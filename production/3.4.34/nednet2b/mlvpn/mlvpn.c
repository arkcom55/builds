/*
 * Copyright (c) 2015, Laurent COUSTET <ed@zehome.com>
 *
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

// GLOBALS 
int                 gServerMode = -1;
char *              _progname;
char **             saved_argv;

mlvpn_tuntap_t      tuntap;
rtun_list_t         rtuns = {NULL, NULL};
mlvpnChannel_t      channels[MLVPN_NUM_CHANNELS];

struct ev_loop *    loop;
//ev_timer          reorder_drain_timeout;
ev_timer            hundred_ms_timeout;
ev_timer            ten_ms_timeout;
char*               process_title = NULL;
int                 logdebug = 0;
uint64_t            data_seq = 0;

mlvpn_tunnel_t* dslTunnel = NULL;
mlvpn_tunnel_t* lteTunnel = NULL;

struct mlvpn_status_s mlvpn_status = {
    .start_time = 0,
    .last_reload = 0,
    .fallback_mode = 0,
    .connected = 0,
    .initialized = 0
};

/********************************************************************
 * Program options
 *-------------------------------------------------------------------
 */
struct mlvpn_options_s mlvpn_options = {
    .change_process_title = 1,
    .process_name = "mlvpn",
    .control_unix_path = "",
    .control_bind_host = "",
    .control_bind_port = "",
    .ip4 = "",
    .ip6 = "",
    .ip4_gateway = "",
    .ip6_gateway = "",
    .mtu = 0,
    .config_path = "mlvpn.conf",
    .config_fd = -1,
    .debug = 0,
    .verbose = 1,
    .reorder_buffer_size = 32,
    .reorder_buffer_timeout_ms = 200,
    .tcp_reorder_buffer_size = 0,
    .tcp_reorder_buffer_timeout_ms = 0,
    .initial_speedtest = 1,
    .autospeed = 0,
    .dslWeight = 0
};

char *optstr = "c:dhv:V";
struct option long_options[] = {
    {"config",        required_argument, 0, 'c' },
    {"debug",         no_argument,       0, 'd' },
    {"help",          no_argument,       0, 'h' },
    {"verbose",       required_argument, 0, 'v' },
    {"version",       no_argument,       0, 'V' },
    {0,               0,                 0, 0 }
};

/********************************************************************
 * Function: usage
 *-------------------------------------------------------------------
 * Process packet received on a tunnel
 *-------------------------------------------------------------------
 */
void usage(char **argv) {
    fprintf(
        stderr,
        "usage: %s [options]\n\n"
            "Options:\n"
            " -c, --config [path]   path to config file\n"
            " -d, --debug           don't use syslog, print to stdout\n"
            " -h, --help            this help\n"
            " -v, --verbose [level] verbosity (default 2)\n"
            " -V, --version         output version information and exit\n"
            "\n",
        argv[0]
    );
    exit(2);
}

/********************************************************************
 * Function: mlvpn_rtun_new
 *-------------------------------------------------------------------
 * Description:
 *  Create a new tunnel
 *-------------------------------------------------------------------
 */
mlvpn_tunnel_t* mlvpn_rtun_new(
    const char* name,
    const char* bindaddr, 
    const char* bindport, 
    uint32_t    bindfib,
    const char* destaddr, 
    const char* destport,
    uint32_t    bandwidth
) {
    mlvpn_tunnel_t* tp = NULL;

    // Some basic checks
    if (gServerMode) {
        if (bindport == NULL) {
            log_warnx(NULL, "Cannot initialize socket without bindport");
            return NULL;
        }
    } else {
        if (destaddr == NULL || destport == NULL) {
            log_warnx(NULL, "Cannot initialize socket without destaddr or destport");
            return NULL;
        }
    }

    tp = (mlvpn_tunnel_t*)calloc(1, sizeof(*tp));
    if (!tp) {
        fatal(NULL, "calloc failed");
    }
    
    // Other values are enforced by calloc to 0/NULL
    tp->name = strdup(name);
    tp->fd = -1;
    tp->status = MLVPN_DISCONNECTED;
    tp->timeout = mlvpn_options.timeout;
    tp->addrinfo = NULL;
    tp->seq = 0;
    tp->expected_receiver_seq = 0;
    tp->seq_last = 0;
    tp->bandwidth = bandwidth;
    if (bindaddr) {
        mystrlcpy(tp->bindaddr, bindaddr, sizeof(tp->bindaddr));
    }
    if (bindport) {
        mystrlcpy(tp->bindport, bindport, sizeof(tp->bindport));
    }
    tp->bindfib = bindfib;
    if (destaddr) {
        mystrlcpy(tp->destaddr, destaddr, sizeof(tp->destaddr));
    }
    if (destport) {
        mystrlcpy(tp->destport, destport, sizeof(tp->destport));
    }
    
    // Read device
    tp->io_read.data = tp;
    ev_init(&tp->io_read, mlvpn_rtun_read);

    // IO timeout
    tp->io_timeout.data = tp;
    ev_timer_init(
        &tp->io_timeout, 
        mlvpn_rtun_check_timeout,
        0., 
        1.
    );
    ev_timer_start(EV_A_ &tp->io_timeout);
    
    // Link in tunnel, link first if dsl
    if (rtuns.first && !strncmp(tp->name, "dsl", 3)) {
        // Link at start
        tp->previous = NULL;
        tp->next = rtuns.first;
        rtuns.first->previous = tp;
        rtuns.first = tp;
    } else {
        // Link at end
        tp->previous = rtuns.last;
        if (rtuns.last) {
            rtuns.last->next = tp;
        } else {
            rtuns.first = tp;
        }
        rtuns.last = tp;
    }
    
    update_process_title();
    
    return tp;
}

/********************************************************************
 * Function: mlvpn_rtun_drop
 *-------------------------------------------------------------------
 * Destroy a tunnel
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_drop(mlvpn_tunnel_t* tp) {
    // Disconnect tunnela nd stop and devices
    mlvpn_rtun_send_disconnect(tp);
    mlvpn_rtun_status_down(tp);
    ev_timer_stop(EV_A_ &tp->io_timeout);
    ev_io_stop(EV_A_ &tp->io_read);

    // Free any allocated members of tunnel
    if (tp->name) {
        free(tp->name);
    }
    if (tp->addrinfo) {
        freeaddrinfo(tp->addrinfo);
    }
    
    // Unlink tunnel
    if (tp->previous) {
        tp->previous->next = tp->next;
    } else {
        rtuns.first = tp->next;
    }
    if (tp->next) {
        tp->next->previous = tp->previous;
    } else {
        rtuns.last = tp->previous;
    }

    update_process_title();
}

/********************************************************************
 * Function: mlvpn_rtun_bind
 *-------------------------------------------------------------------
 * Bind tunnel to an address.
 *-------------------------------------------------------------------
 */
int mlvpn_rtun_bind(mlvpn_tunnel_t *tp) {
    struct addrinfo hints, *res;
    int n, fd;

    memset(&hints, 0, sizeof(hints));
    /* AI_PASSIVE flag: the resulting address is used to bind
       to a socket for accepting incoming connections.
       So, when the hostname==NULL, getaddrinfo function will
       return one entry per allowed protocol family containing
       the unspecified address for that family. */
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    fd = tp->fd;

    n = priv_getaddrinfo(tp->bindaddr, tp->bindport, &res, &hints);
    if (n <= 0) {
        log_warnx(NULL, "%s getaddrinfo error: %s", tp->name, gai_strerror(n));
        return -1;
    }

    /* Try open socket with each address getaddrinfo returned,
       until getting a valid listening socket. */
    log_info(NULL, "%s bind to %s", tp->name, *tp->bindaddr ? tp->bindaddr : "any");
    n = bind(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (n < 0) {
        log_warn(NULL, "%s bind error", tp->name);
        return -1;
    }
    return 0;
}

/********************************************************************
 * Function: mlvpn_rtun_start
 *-------------------------------------------------------------------
 * Start a tunnel
 *-------------------------------------------------------------------
 */
int mlvpn_rtun_start(mlvpn_tunnel_t* tp) {
    int ret, fd = -1;
    char *addr, *port;
    struct addrinfo hints, *res;
#if defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD)
    int fib = tp->bindfib;
#endif
    fd = tp->fd;
    if (gServerMode) {
        addr = tp->bindaddr;
        port = tp->bindport;
    } else {
        addr = tp->destaddr;
        port = tp->destport;
    }

    /* Initialize hints */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    ret = priv_getaddrinfo(addr, port, &tp->addrinfo, &hints);
    if (ret <= 0 || !tp->addrinfo) {
        log_warnx("dns", "%s getaddrinfo(%s,%s) failed: %s",
           tp->name, addr, port, gai_strerror(ret));
        return -1;
    }

    res = tp->addrinfo;
    while (res) {
        /* creation de la socket(2) */
        if ( (fd = socket(tp->addrinfo->ai_family,
                          tp->addrinfo->ai_socktype,
                          tp->addrinfo->ai_protocol)) < 0) {
            log_warn(NULL, "%s socket creation error", tp->name);
        } else {
            /* Setting fib/routing-table is supported on FreeBSD and OpenBSD only */
#if defined(HAVE_FREEBSD)
            if (fib > 0 && setsockopt(fd, SOL_SOCKET, SO_SETFIB, &fib, sizeof(fib)) < 0)
#elif defined(HAVE_OPENBSD)
            if (fib > 0 && setsockopt(fd, SOL_SOCKET, SO_RTABLE, &fib, sizeof(fib)) < 0)
            {
                log_warn(NULL, "Cannot set FIB %d for kernel socket", fib);
                goto error;
            }
#endif
            tp->fd = fd;
            break;
        }
        res = res->ai_next;
    }

    if (fd < 0) {
        log_warnx("dns", "%s connection failed. Check DNS?", tp->name);
        goto error;
    }

    /* setup non blocking sockets */
    socklen_t val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(socklen_t)) < 0) {
        log_warn(NULL, "%s setsockopt SO_REUSEADDR failed", tp->name);
        goto error;
    }
    if (*tp->bindaddr) {
        if (mlvpn_rtun_bind(tp) < 0) {
            log_warn("rtun", "%s mlvpn_rtun_bind returns error", tp->name);
            goto error;
        }
    }

    /* set non blocking after connect... May lockup the entire process */
    mlvpn_sock_set_nonblocking(fd);
    ev_io_set(&tp->io_read, fd, EV_READ);
    ev_io_start(EV_A_ &tp->io_read);
    return 0;

error:
    if (tp->fd > 0) {
        close(tp->fd);
        tp->fd = -1;
    }
    return -1;
}

/********************************************************************
 * Function: mlvpn_script_get_env
 *-------------------------------------------------------------------
 * Get environment
 *-------------------------------------------------------------------
 */
void mlvpn_script_get_env(int *env_len, char ***env) {
    char **envp;
    int arglen;
    *env_len = 6;
    
    *env = (char **)calloc(*env_len + 1, sizeof(char *));
    if (!*env) {
        fatal(NULL, "out of memory");
    }
    envp = *env;
    
    arglen = sizeof(mlvpn_options.ip4) + 4;
    envp[0] = calloc(1, arglen + 1);
    if (snprintf(envp[0], arglen, "IP4=%s", mlvpn_options.ip4) < 0) {
        log_warn(NULL, "snprintf IP4= failed");
    }
    
    arglen = sizeof(mlvpn_options.ip6) + 4;
    envp[1] = calloc(1, arglen + 1);
    if (snprintf(envp[1], arglen, "IP6=%s", mlvpn_options.ip6) < 0) {
        log_warn(NULL, "snprintf IP6= failed");
    }

    arglen = sizeof(mlvpn_options.ip4_gateway) + 12;
    envp[2] = calloc(1, arglen + 1);
    if (snprintf(envp[2], arglen, "IP4_GATEWAY=%s", mlvpn_options.ip4_gateway) < 0) {
        log_warn(NULL, "snprintf IP4_GATEWAY= failed");
    }
    
    arglen = sizeof(mlvpn_options.ip6_gateway) + 12;
    envp[3] = calloc(1, arglen + 1);
    if (snprintf(envp[3], arglen, "IP6_GATEWAY=%s", mlvpn_options.ip6_gateway) < 0) {
        log_warn(NULL, "snprintf IP6_GATEWAY= failed");
    }

    arglen = sizeof(tuntap.devname) + 7;
    envp[4] = calloc(1, arglen + 1);
    if (snprintf(envp[4], arglen, "DEVICE=%s", tuntap.devname) < 0) {
        log_warn(NULL, "snprintf DEVICE= failed");
    }

    envp[5] = calloc(1, 16);
    if (snprintf(envp[5], 15, "MTU=%d", mlvpn_options.mtu) < 0) {
        log_warn(NULL, "snprintf MTU= failed");
    }
    
    envp[6] = NULL;
}

/********************************************************************
 * Function: mlvpn_free_script_env
 *-------------------------------------------------------------------
 * Free environment
 *-------------------------------------------------------------------
 */
void mlvpn_free_script_env(char **env) {
    char **envp = env;
    while (*envp) {
        free(*envp);
        envp++;
    }
    free(env);
}

/********************************************************************
 * Function: mlvpn_rtun_status_up
 *-------------------------------------------------------------------
 * Bring tunnel up
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_status_up(mlvpn_tunnel_t* tp) {
    PROFILE_ENTER(PROFILE_RTUN_STATUS_UP);
    
    char *cmdargs[4] = {tuntap.devname, "rtun_up", tp->name, NULL};
    char **env;
    int env_len;

    tp->initTime = time(NULL);
    tp->status = MLVPN_AUTHOK;
    tp->timeoutRunning = tp->timeout;
    tp->seq = 0;
    tp->expected_receiver_seq = 0;
    tp->seq_last = 0;
    memset(&tp->speedTest, 0, sizeof(tp->speedTest));
    mlvpn_update_status();
    //mlvpn_rtun_wrr_reset(&rtuns, mlvpn_status.fallback_mode);
    mlvpn_script_get_env(&env_len, &env);
    priv_run_script(3, cmdargs, env_len, env);
    if (mlvpn_status.connected > 0 && mlvpn_status.initialized == 0) {
        cmdargs[0] = tuntap.devname;
        cmdargs[1] = "tuntap_up";
        cmdargs[2] = NULL;
        data_seq = 0;
        priv_run_script(2, cmdargs, env_len, env);
        mlvpn_status.initialized = 1;
        mlvpn_reorder_reinit();
    }
    mlvpn_free_script_env(env);
    mlvpn_clear_tunnel_stats(tp);
    update_process_title();
    
    PROFILE_EXIT(PROFILE_RTUN_STATUS_UP);
}

/********************************************************************
 * Function: mlvpn_rtun_status_down
 *-------------------------------------------------------------------
 * Bring tunnel down
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_status_down(mlvpn_tunnel_t *tp) {
    PROFILE_ENTER(PROFILE_RTUN_STATUS_DOWN);
    
    char *cmdargs[4] = {tuntap.devname, "rtun_down", tp->name, NULL};
    char **env;
    int env_len;
    enum chap_status old_status = tp->status;
    tp->status = MLVPN_DISCONNECTED;
    tp->disconnects++;
    
    mlvpn_update_status();
    if (old_status >= MLVPN_AUTHOK) {
        mlvpn_script_get_env(&env_len, &env);
        priv_run_script(3, cmdargs, env_len, env);
        // Re-initialize weight round robin
        //mlvpn_rtun_wrr_reset(&rtuns, mlvpn_status.fallback_mode);
        if (mlvpn_status.connected == 0 && mlvpn_status.initialized == 1) {
            cmdargs[0] = tuntap.devname;
            cmdargs[1] = "tuntap_down";
            cmdargs[2] = NULL;
            priv_run_script(2, cmdargs, env_len, env);
            mlvpn_status.initialized = 0;
        }
        mlvpn_free_script_env(env);
    }
    mlvpn_clear_tunnel_stats(tp);
    update_process_title();
    
    PROFILE_EXIT(PROFILE_RTUN_STATUS_DOWN);
}

/********************************************************************
 * Function: mlvpn_update_status
 *-------------------------------------------------------------------
 * Update tunnel status
 *-------------------------------------------------------------------
 */
void mlvpn_update_status() {
    //mlvpn_status.fallback_mode = mlvpn_options.fallback_available;
    mlvpn_status.connected = 0;
    for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
        if (tp->status >= MLVPN_AUTHOK) {
            //if (!tp->fallback_only)
            //    mlvpn_status.fallback_mode = 0;
            mlvpn_status.connected++;
            if (!strncmp(tp->name, "dsl", 3)) {
                dslTunnel = tp;
            } else {
                lteTunnel = tp;
            }
        } else {
            if (!strncmp(tp->name, "dsl", 3)) {
                dslTunnel = NULL;
            } else {
                lteTunnel = NULL;
            }
        }
    }
}

/********************************************************************
 * Function: mlvpn_rtun_tick_connect
 *-------------------------------------------------------------------
 * Initiate tunnel connections
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_tick_connect(mlvpn_tunnel_t *tp) {
    //log_info("ARK", "%s mlvpn_rtun_tick_connect", tp->name);

    if (gServerMode) {
        if (tp->fd < 0) {
            if (mlvpn_rtun_start(tp) == 0) {
                tp->conn_attempts = 0;
            } else {
                return;
            }
        }
    } else {
        if (tp->status < MLVPN_AUTHOK) {
            tp->conn_attempts++;
            if (tp->fd < 0) {
                if (mlvpn_rtun_start(tp) == 0) {
                    tp->conn_attempts = 0;
                } else {
                    return;
                }
            }
        }
        mlvpn_rtun_challenge_send(tp);
    }
}

/********************************************************************
 * Function: mlvpn_rtun_check_timeout
 *-------------------------------------------------------------------
 * Check tunnel timeouts
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_check_timeout(EV_P_ ev_timer *w, int revents) {
    mlvpn_tunnel_t* tp = w->data;
    //log_info("ARK", "%s mlvpn_rtun_check_timeout: status:%d", tp->name, tp->status);
    
    if (tp->status >= MLVPN_AUTHOK) {
        if (tp->timeoutRunning > 0) {
            tp->timeoutRunning--;
            if (!tp->timeoutRunning) {
                log_info("protocol", "%s timeout", tp->name);
                mlvpn_rtun_status_down(tp);
                return;
            }
        }
        if (gServerMode) {
            mlvpn_rtun_send_keepalive(tp);
        }
    } else {
        mlvpn_rtun_tick_connect(tp);
    }
}

/********************************************************************
 * Function: mlvpn_hundred_ms_timeout
 *-------------------------------------------------------------------
 * 100 ms timeout
 *-------------------------------------------------------------------
 */
void mlvpn_hundred_ms_timeout(EV_P_ ev_timer *w, int revents) {
    //log_info("ARK", "mlvpn_hundred_ms_timeout");
    PROFILE_ENTER(PROFILE_100_MS);
    mlvpn100ms();
    PROFILE_EXIT(PROFILE_100_MS);
}

/********************************************************************
 * Function: mlvpn_ten_ms_timeout
 *-------------------------------------------------------------------
 * 100 ms timeout
 *-------------------------------------------------------------------
 */
void mlvpn_ten_ms_timeout(EV_P_ ev_timer *w, int revents) {
    //log_info("ARK", "mlvpn_ten_ms_timeout");
    PROFILE_ENTER(PROFILE_10_MS);
    mlvpn_reorder_flush();
    mlvpn10ms();
    PROFILE_EXIT(PROFILE_10_MS);
}

/********************************************************************
 * Function: tuntap_io_event_read
 *-------------------------------------------------------------------
 * Handle tap read and write events
 *-------------------------------------------------------------------
 */
void tuntap_io_event_read(EV_P_ ev_io *w, int revents) {
    mlvpn_tuntap_read(&tuntap);
}

/********************************************************************
 * Function: tuntap_io_event_write
 *-------------------------------------------------------------------
 * Handle tap read and write events
 *-------------------------------------------------------------------
 */
void tuntap_io_event_write(EV_P_ ev_io *w, int revents) {
    while (mlvpn_queue_count(&tuntap.sbuf)) {
        mlvpn_tuntap_write(&tuntap);
    }
    if (!mlvpn_queue_count(&tuntap.sbuf)) {
        ev_io_stop(EV_A_ &tuntap.io_write);
    }
}

/********************************************************************
 * Function: mlvpn_tuntap_init
 *-------------------------------------------------------------------
 * Initialize tap
 *-------------------------------------------------------------------
 */
void mlvpn_tuntap_init() {
    memset(&tuntap, 0, sizeof(tuntap));
    snprintf(tuntap.devname, MLVPN_IFNAMSIZ-1, "%s", "mlvpn0");
    tuntap.maxmtu = MLVPN_MTU;
    log_debug(NULL, "absolute maximum mtu: %d", tuntap.maxmtu);
    tuntap.type = MLVPN_TUNTAPMODE_TUN;
    ev_init(&tuntap.io_read, tuntap_io_event_read);
    ev_init(&tuntap.io_write, tuntap_io_event_write);
}

/********************************************************************
 * Function: update_process_title
 *-------------------------------------------------------------------
 * Update process title
 *-------------------------------------------------------------------
 */
void update_process_title() {
    char title[1024];
    char *s;
    char status[32];
    int len;
    
    memset(title, 0, sizeof(title));
    if (!process_title) {
        strcpy(title, tuntap.devname);
    } else if (*process_title) {
        mystrlcat(title, process_title, sizeof(title));
    }
    
    for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
        switch(tp->status) {
            case MLVPN_AUTHOK:
                s = "@";
                break;
            case MLVPN_LOSSY:
                s = "~";
                break;
            default:
                s = "!";
                break;
        }
        len = snprintf(status, sizeof(status) - 1, " %s%s", s, tp->name);
        if (len) {
            status[len] = 0;
            mystrlcat(title, status, sizeof(title));
        }
    }
    
    setproctitle("%s", title);
}

/********************************************************************
 * Function: mlvpn_quit
 *-------------------------------------------------------------------
 * Shutdown MLVPN
 *-------------------------------------------------------------------
 */
void mlvpn_quit(EV_P_ ev_signal *w, int revents) {
    log_info(NULL, "killed by signal SIGTERM, SIGQUIT or SIGINT");
    
    for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
        ev_timer_stop(EV_A_ &tp->io_timeout);
        ev_io_stop(EV_A_ &tp->io_read);
        if (tp->status >= MLVPN_AUTHOK) {
            mlvpn_rtun_send_disconnect(tp);
        }
    }
   
    ev_break(EV_A_ EVBREAK_ALL);
}

/********************************************************************
 * Function: idle_cb
 *-------------------------------------------------------------------
 */
 
void idle_cb(
    struct ev_loop *loop, 
    ev_idle *w, 
    int revents
) {
    PROFILE_ENTER(PROFILE_IDLE);
    
    // Send any queued tunnel data
    for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
        while (mlvpn_queue_count(&tp->hsbuf)) {
            log_debug("TUN", "%s found hsbuf", tp->name);
            
            // Check if anything to send
            mlvpn_pkt_t* pp = mlvpn_queue_get(&tp->hsbuf);
            if (pp) {
                mlvpn_rtun_send_pkt(tp, pp);
            } else {
                break;
            }
        }
    }
    
    // Check for any packets waiting for sending from the RTAP 
    if (mlvpn_queue_count(&tuntap.rbuf)) {
        mlvpn_tunnel_t* tp = NULL;
        
        // Check if any tunnels available
        uint64_t ctime = mlvpn_timestamp_usec();
        if ((dslTunnel && dslTunnel->bandwidth && !dslTunnel->speedTest.running) && (lteTunnel && lteTunnel->bandwidth && !lteTunnel->speedTest.running)) {
            // Both tunnels available
            
            // Determine target number of queued packets for DSL preference (100 ms of packets)
            int number;
            if (mlvpn_options.dslWeight > 0) {
                number = mlvpn_options.dslWeight;
            } else {
                number = dslTunnel->bandwidth / (8 * 10);
            }
            
            if (mlvpn_queue_count(&tuntap.rbuf) < number) {
                tp = dslTunnel;
            } else {
                // Use DSL if available
                if (ctime >= dslTunnel->nextTxTimeUsec) {
                    tp = dslTunnel;
                } else {
                    tp = lteTunnel;
                }
            }
        } else if (dslTunnel && dslTunnel->bandwidth && !dslTunnel->speedTest.running) {
            // DSL tunnel only
            tp = dslTunnel;
        } else if (lteTunnel && lteTunnel->bandwidth && !lteTunnel->speedTest.running) {
            // LTE tunnel only
            tp = lteTunnel;
        }
        else {
            // No tunnels available
        }
        
        if (tp && ctime >= tp->nextTxTimeUsec) {
            mlvpn_pkt_t* pp = mlvpn_queue_get(&tuntap.rbuf);

            //tp->nextTxTimeUsec = ctime + ((pp->protocol.dataLength + MLVPN_PROTO_OVERHEAD) * 8000) / (tp->bandwidth);
            uint64_t dtime = ctime - tp->nextTxTimeUsec;
            uint64_t ptime = ((pp->protocol.dataLength + MLVPN_PROTO_OVERHEAD) * 8000) / tp->bandwidth;
            if (dtime > ptime) {
                tp->nextTxTimeUsec = ctime + ptime;
            } else {
                tp->nextTxTimeUsec += ptime;
            }

            mlvpn_rtun_send_pkt(tp, pp);
        }
    }

    usleep(10);
    PROFILE_EXIT(PROFILE_IDLE);
}

/********************************************************************
 * Function: mlvpn_sock_set_nonblocking
 *-------------------------------------------------------------------
 * Set socket nonblocking.
 *-------------------------------------------------------------------
 */
int mlvpn_sock_set_nonblocking(int fd) {
    int ret = 0;
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) {
        log_warn(NULL, "fcntl");
        ret = -1;
    } else {
        fl |= O_NONBLOCK;
        if ((ret = fcntl(fd, F_SETFL, fl)) < 0) {
            log_warn(NULL, "Unable to set socket %d non blocking", fd);
        }
    }
    return ret;
}

/********************************************************************
 * Function: mlvpn_queue_put
 *-------------------------------------------------------------------
 * Put packet onto queue
 *-------------------------------------------------------------------
 */
int mlvpn_queue_put(mlvpn_pkt_buffer_t* queue, mlvpn_pkt_t* pp) {
    if (!queue || !pp) {
        goto errorExit;
    }
    
    pp->previous = queue->last;
    pp->next = NULL;
    if (queue->last) {
        queue->last->next = pp;
    } else {
        queue->first = pp;
    }
    queue->last = pp;
    queue->count++;
    
  errorExit:
    return ERR_CODE_OK;
}

/********************************************************************
 * Function: mlvpn_sock_set_nonblocking
 *-------------------------------------------------------------------
 * Set socket nonblocking.
 *-------------------------------------------------------------------
 */
mlvpn_pkt_t* mlvpn_queue_get(mlvpn_pkt_buffer_t* queue) {
    mlvpn_pkt_t* pp = NULL;
    
    if (!queue || !queue->first) {
        goto errorExit;
    }
    pp = queue->first;
    if (pp->next) {
        pp->next->previous = NULL;
    } else {
        queue->last = NULL;
    }
    queue->first = pp->next;
    queue->count--;
    
  errorExit:
    return pp;
}

/********************************************************************
 * Function: mlvpn_sock_set_nonblocking
 *-------------------------------------------------------------------
 * Set socket nonblocking.
 *-------------------------------------------------------------------
 */
int mlvpn_queue_count(mlvpn_pkt_buffer_t* queue) {
    int count = 0;
    
    if (!queue) {
        goto errorExit;
    }
    
    count = queue->count;
    
  errorExit:
    return count;
}

/********************************************************************
 * Function: main
 *-------------------------------------------------------------------
 * Main rotine
 *-------------------------------------------------------------------
 */
int main(int argc, char **argv) {
    PROFILE_ENTER(PROFILE_MAIN);
    
    int i;
    extern char *__progname;

    int c, option_index, config_fd;
    //ev_signal signal_hup;
    ev_signal signal_sigquit, signal_sigint, signal_sigterm;
    
#ifdef ENABLE_CONTROL
    struct mlvpn_control control;
#endif

    /* uptime statistics */
    if (time(&mlvpn_status.start_time) == -1) {
        log_warn(NULL, "start_time time() failed");
    }
    
    if (time(&mlvpn_status.last_reload) == -1) {
        log_warn(NULL, "last_reload time() failed");
    }
    
    log_init(1, 2, "mlvpn");

    _progname = strdup(__progname);
    saved_argv = calloc(argc + 1, sizeof(*saved_argv));
    for (i = 0; i < argc; i++) {
        saved_argv[i] = strdup(argv[i]);
    }
    saved_argv[i] = NULL;
    compat_init_setproctitle(argc, argv);
    argv = saved_argv;

    // Parse the command line
    while(1) {
        c = getopt_long(
            argc, 
            argv, 
            //saved_argv, 
            optstr,
            long_options, 
            &option_index
        );
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'c': /* --config */
                mystrlcpy(
                    mlvpn_options.config_path, 
                    optarg,
                    sizeof(mlvpn_options.config_path)
                );
                break;
            case 'd': /* debug= */
                mlvpn_options.debug = 1;
                break;
            case 'v': /* --verbose */
                mlvpn_options.verbose = atoi(optarg);
                break;
            case 'V': /* --version */
                printf("mlvpn version %s.\n", VERSION);
                _exit(0);
                break;
            case 'h': /* --help */
            default:
                usage(argv);
        }
    }

    /* Config file check */
    if (access(mlvpn_options.config_path, R_OK) != 0) {
        log_warnx("config", "unable to read config file %s", mlvpn_options.config_path);
    }

#ifdef HAVE_LINUX
    if (access("/dev/net/tun", R_OK|W_OK) != 0) {
        fatal(NULL, "unable to open /dev/net/tun");
    }
#endif

    if (MlvpnPacketsAllocate()) {
        fatal(NULL, "unable to allocate packet buffers");
    }

    log_init(mlvpn_options.debug, mlvpn_options.verbose, mlvpn_options.process_name);

#ifdef HAVE_LINUX
    mlvpn_systemd_notify();
#endif

    /* Config file opening/parsing */
    config_fd = open(mlvpn_options.config_path, O_RDONLY);
    if (config_fd < 0) {
        fatalx("cannot open config file");
    }
    if (!(loop = ev_default_loop(EVFLAG_AUTO))) {
        fatal(NULL, "cannot initialize libev. check LIBEV_FLAGS?");
    }
    
    /* tun/tap initialization */
    mlvpn_tuntap_init();
    if (mlvpn_config(config_fd) != 0) {
        fatalx("cannot open config file");
    }
    
    if (mlvpn_tuntap_alloc(&tuntap) <= 0) {
        fatalx("cannot create tunnel device");
    } else {
        log_info(NULL, "created interface `%s'", tuntap.devname);
    }
    mlvpn_sock_set_nonblocking(tuntap.fd);

    //reorder_drain_timeout.repeat = 0.2;
    //ev_init(&reorder_drain_timeout, &mlvpn_rtun_reorder_drain_timeout);
    
    ev_io_set(&tuntap.io_read, tuntap.fd, EV_READ);
    ev_io_set(&tuntap.io_write, tuntap.fd, EV_WRITE);
    ev_io_start(loop, &tuntap.io_read);

    ev_timer_init(
        &hundred_ms_timeout,
        mlvpn_hundred_ms_timeout, 
        0., 
        0.1
    );
    ev_timer_start(EV_A_ &hundred_ms_timeout);

    ev_timer_init(
        &ten_ms_timeout,
        mlvpn_ten_ms_timeout, 
        0.,
        0.01
    );
    ev_timer_start(EV_A_ &ten_ms_timeout);

#ifdef ENABLE_CONTROL
    /* Initialize mlvpn remote control system */
    if (gServerMode) {
        mystrlcpy(control.fifo_path, mlvpn_options.control_unix_path,
            sizeof(control.fifo_path));
        control.mode = MLVPN_CONTROL_READWRITE;
        control.fifo_mode = 0600;
        control.bindaddr = strdup(mlvpn_options.control_bind_host);
        control.bindport = strdup(mlvpn_options.control_bind_port);
        mlvpn_control_init(&control);
    }
#endif

    //ev_signal_init(&signal_hup, mlvpn_config_reload, SIGHUP);
    ev_signal_init(&signal_sigint, mlvpn_quit, SIGINT);
    ev_signal_init(&signal_sigquit, mlvpn_quit, SIGQUIT);
    ev_signal_init(&signal_sigterm, mlvpn_quit, SIGTERM);
    //ev_signal_start(loop, &signal_hup);
    ev_signal_start(loop, &signal_sigint);
    ev_signal_start(loop, &signal_sigquit);
    ev_signal_start(loop, &signal_sigterm);

    ev_idle *idle_watcher = malloc(sizeof(ev_idle));
    ev_idle_init(idle_watcher, idle_cb);
    ev_idle_start(loop, idle_watcher);
    
    log_info("ARK", "Starting main event loop.");
    ev_run(loop, 0);

    PROFILE_EXIT(PROFILE_MAIN);
#if PROFILE
    ProfileDump(tuntap.devname);
#endif
    return 0;
}
