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

//extern struct mlvpn_options_s mlvpn_options;

/* Config file reading 
 * config_file_fd: fd 
 */
int mlvpn_config(int config_file_fd) {
    config_t*   config;
    config_t*   work;
    char*       tmp;
    uint32_t    tmpNumber;
    char*       section = "";
    
    work = config = _conf_parseConfig(config_file_fd);
    if (!config) {
        log_info("CFG", "Error parsing config file.");
        goto errorExit;
    }

    while (work) {
        if ((work->section != NULL) && !mystr_eq(work->section, section)) {
            section = work->section;
            if (mystr_eq(section, "general")) {
                // Mode
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "mode", 
                    &tmp, 
                    NULL,
                    "Operation mode is mandatory.", 
                    1
                );
                if (tmp) {
                    if (mystr_eq(tmp, "server")) {
                        gServerMode = 1;
                    } else if (mystr_eq(tmp, "client")) {
                        gServerMode = 0;
                    }
                    free(tmp);
                }
                
                // Interface name
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "interface_name", 
                    &tmp, 
                    "mlvpn0",
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(tuntap.devname, tmp, sizeof(tuntap.devname));
                    free(tmp);
                }
                
                // TAP or TUN
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "tuntap", 
                    &tmp, 
                    "tun", 
                    NULL, 
                    0
                );
                if (tmp) {
                    if (mystr_eq(tmp, "tun")) {
                        tuntap.type = MLVPN_TUNTAPMODE_TUN;
                    } else {
                        tuntap.type = MLVPN_TUNTAPMODE_TAP;
                    }
                    free(tmp);
                }
                
                // Status command
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "statuscommand", 
                    &tmp, 
                    NULL, 
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.status_command, tmp, sizeof(mlvpn_options.status_command));
                    free(tmp);
                }
                
                // Control configuration
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "control_unix_path", &tmp, 
                    NULL,
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.control_unix_path, tmp, sizeof(mlvpn_options.control_unix_path));
                    free(tmp);
                }
                
                // Control bind host
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "control_bind_host", 
                    &tmp, 
                    NULL,
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.control_bind_host, tmp, sizeof(mlvpn_options.control_bind_host));
                    free(tmp);
                }
                
                // Control bind port
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "control_bind_port", 
                    &tmp, 
                    NULL,
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.control_bind_port, tmp, sizeof(mlvpn_options.control_bind_port));
                    free(tmp);
                }
                
                // Password
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "password", 
                    &tmp, 
                    NULL,
                    "Password is mandatory.", 
                    2
                );
                if (tmp) {
                    log_info("config", "new password set");
                    //crypto_set_password(tmp, strlen(tmp));
                    memset(tmp, 0, strlen(tmp));
                    free(tmp);
                }
                
                // Default timeout
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "timeout", 
                    &tmpNumber, 
                    60,
                    NULL, 
                    0
                );
                if (tmpNumber < 2) {
                    log_warnx("config", "timeout capped to 2 seconds");
                    tmpNumber = 2;
                }
                mlvpn_options.timeout = tmpNumber;
                
                // Reorder buffer size
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "reorder_buffer_size",
                    &tmpNumber,
                    0, 
                    NULL, 
                    0
                );
                mlvpn_options.reorder_buffer_size = tmpNumber;

                // Reorder buffer timeout
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "reorder_buffer_timeout",
                    &tmpNumber,
                    0, 
                    NULL, 
                    0
                );
                mlvpn_options.reorder_buffer_timeout_ms = (uint16_t)tmpNumber;

                // TCP Reorder buffer size
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "tcp_reorder_buffer_size",
                    &tmpNumber,
                    0, 
                    NULL, 
                    0
                );
                mlvpn_options.tcp_reorder_buffer_size = tmpNumber;

                // TCP reorder buffer timeout
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "tcp_reorder_buffer_timeout",
                    &tmpNumber,
                    0, 
                    NULL, 
                    0
                );
                mlvpn_options.tcp_reorder_buffer_timeout_ms = (uint16_t)tmpNumber;

                // IPv4 for device
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "ip4", 
                    &tmp, 
                    NULL, 
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.ip4, tmp, sizeof(mlvpn_options.ip4));
                    free(tmp);
                } else {
                    memset(mlvpn_options.ip4, 0, sizeof(mlvpn_options.ip4));
                }

                // IPv6 for device
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "ip6", 
                    &tmp, 
                    NULL, 
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.ip6, tmp, sizeof(mlvpn_options.ip6));
                    free(tmp);
                } else {
                    memset(mlvpn_options.ip6, 0, sizeof(mlvpn_options.ip6));
                }

                // IPv4 gateway
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "ip4_gateway", 
                    &tmp, 
                    NULL, 
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.ip4_gateway, tmp, sizeof(mlvpn_options.ip4_gateway));
                    free(tmp);
                } else {
                    memset(mlvpn_options.ip4_gateway, 0, sizeof(mlvpn_options.ip4_gateway));
                }

                // IPv6 gateway
                _conf_set_str_from_conf(
                    config, 
                    section, 
                    "ip6_gateway", 
                    &tmp, 
                    NULL, 
                    NULL, 
                    0
                );
                if (tmp) {
                    mystrlcpy(mlvpn_options.ip6_gateway, tmp, sizeof(mlvpn_options.ip6_gateway));
                    free(tmp);
                } else {
                    memset(mlvpn_options.ip6_gateway, 0, sizeof(mlvpn_options.ip6_gateway));
                }

                // MTU
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "mtu", 
                    &tmpNumber, 
                    MLVPN_MTU, 
                    NULL, 
                    0
                );
                if (tmpNumber != 0) {
                    mlvpn_options.mtu = tmpNumber;
                }
                
                // Initial speedtest
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "initial_speedtest", 
                    &tmpNumber, 
                    mlvpn_options.initial_speedtest, 
                    NULL, 
                    0
                );
                mlvpn_options.initial_speedtest = tmpNumber;
                
                // Initial speedtest
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "autospeed", 
                    &tmpNumber, 
                    mlvpn_options.autospeed, 
                    NULL, 
                    0
                );
                mlvpn_options.autospeed = tmpNumber;                
                
                // Initial speedtest
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "dsl_weight", 
                    &tmpNumber, 
                    mlvpn_options.dslWeight, 
                    NULL, 
                    0
                );
                mlvpn_options.dslWeight = tmpNumber;                
            } else {
                // Tunnel info
                char*       bindaddr;
                char*       bindport;
                uint32_t    bindfib;
                char*       dstaddr;
                char*       dstport;
                uint32_t    bwlimit;

                if (gServerMode < 0) {
                    log_warnx("config", "server/client mode not defined");
                    goto errorExit;
                }
                
                if (gServerMode)
                {
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "bindhost", 
                        &bindaddr, 
                        NULL,
                        NULL, 
                        0
                    );
                    
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "bindport", 
                        &bindport, 
                        NULL,
                        "bind port is mandatory in server mode.\n", 
                        1
                    );
                    
                    _conf_set_uint_from_conf(
                        config, 
                        section, 
                        "bindfib", 
                        &bindfib, 
                        0,
                        NULL, 
                        0
                    );
                    
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "remotehost", 
                        &dstaddr, 
                        NULL,
                        NULL, 
                        0
                    );
                        
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "remoteport", 
                        &dstport, 
                        NULL,
                        NULL, 
                        0
                    );
                } else {
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "bindhost", 
                        &bindaddr, 
                        NULL,
                        NULL, 
                        0
                    );
                    
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "bindport", 
                        &bindport, 
                        NULL,
                        NULL, 
                        0
                    );
                    
                     _conf_set_uint_from_conf(
                        config, 
                        section, 
                        "bindfib", 
                        &bindfib, 
                        0,
                        NULL, 
                        0
                    );
                    
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "remotehost", 
                        &dstaddr, 
                        NULL,
                        "No remote address specified.\n", 
                        1
                    );
                    
                    _conf_set_str_from_conf(
                        config, 
                        section, 
                        "remoteport", 
                        &dstport, 
                        NULL,
                        "No remote port specified.\n", 
                        1
                    );
                }
                
                _conf_set_uint_from_conf(
                    config, 
                    section, 
                    "bandwidth_upload", 
                    &bwlimit, 
                    0,
                    NULL, 
                    0
                );
                
                log_info("config", "%s tunnel added", section);
                mlvpn_rtun_new(
                    section, 
                    bindaddr, 
                    bindport, 
                    bindfib, 
                    dstaddr, 
                    dstport,
                    bwlimit
                );
                
                if (bindaddr) {
                    free(bindaddr);
                }
                if (bindport) {
                    free(bindport);
                }
                if (dstaddr) {
                    free(dstaddr);
                }
                if (dstport) {
                    free(dstport);
                }
            }
        } else if (section == NULL) {
            section = work->section;
        }
        
        work = work->next;
    }

    //_conf_printConfig(config);
    _conf_freeConfig(config);

    mlvpn_reorder_init(
        mlvpn_options.reorder_buffer_size,
        mlvpn_options.reorder_buffer_timeout_ms
    );
    mlvpn_reorder_init_tcp(
        mlvpn_options.tcp_reorder_buffer_size,
        mlvpn_options.tcp_reorder_buffer_timeout_ms
    );

    return 0;
    
  errorExit:
    log_warnx("config", "parse error");
    return 1;
}
