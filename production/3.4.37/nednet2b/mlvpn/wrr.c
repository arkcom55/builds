/*******************************************************************
 * Function: wrr.c
 *------------------------------------------------------------------
 * Modified 2021-03-09 Andrew Kostiuk
 * - Add tunnel priority and quota system.
 *------------------------------------------------------------------
 */
 
#define SPEEDTEST   (1)

#include "includes.h"

static int sSpeedTestRunning = FALSE;
static int sSpeedTestForce = FALSE;

#define MLVPN_MAX_PACKETS   (2000)
static mlvpn_pkt_t* sFreePacketsFirst = NULL;
static mlvpn_pkt_t* sFreePacketsLast = NULL;
static int sNumFreePackets = 0;

void monitor_ulerror_rate(
    mlvpn_tunnel_t*     tp,
    uint64_t            dataTimestampMsec, 
    uint32_t            packets,
    uint32_t            bytes,
    uint32_t            lost
);

#if PROFILE

Profile_t gProfile[PROFILE_COUNT] = {
    {PROFILE_MAIN,                          "main",                             0, 0, 0, 0, 0, 0},
    
    {PROFILE_TUNTAP_READ,                   "tuntap_read",                      0, 0, 0, 0, 0, 0},
    {PROFILE_TUNTAP_READ_READ,              "  tuntap_read_read",               0, 0, 0, 0, 0, 0},
    
    {PROFILE_IDLE,                          "idle",                             0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_SEND_PKT,                 "  rtun_send_pkt",                  0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_SEND_PKT_SENDTO,          "    rtun_send_pkt_sendto",         0, 0, 0, 0, 0, 0},
    
    {PROFILE_RTUN_READ,                     "rtun_read",                        0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_READ_RECVFROM,            "  rtun_read_recvfrom",             0, 0, 0, 0, 0, 0},
    {PROFILE_PROTOCOL_READ,                 "  protocol_read",                  0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_RECV_DATA,                "  rtun_recv_data",                 0, 0, 0, 0, 0, 0},
    {PROFILE_REORDER,                       "    reorder",                      0, 0, 0, 0, 0, 0},
    {PROFILE_INJECT_TUNTAP,                 "      inject_tuntap",              0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_SEND_AUTH,                "  rtun_send_auth",                 0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_STATUS_UP,                "    rtun_status_up",               0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_SEND_KEEPALIVE_RESPONSE,  "  rtun_send_keepalive_response",   0, 0, 0, 0, 0, 0},

    {PROFILE_TUNTAP_WRITE,                  "tuntap_write",                     0, 0, 0, 0, 0, 0},
    {PROFILE_TUNTAP_WRITE_WRITE,            "  tuntap_write_write",             0, 0, 0, 0, 0, 0},

    {PROFILE_RTUN_STATUS_DOWN,              "rtun_status_down",                 0, 0, 0, 0, 0, 0},
    {PROFILE_RTUN_SEND_KEEPALIVE,           "rtun_send_keepalive",              0, 0, 0, 0, 0, 0},
    {PROFILE_SPEEDTEST_RESET,               "speedtest_reset",                  0, 0, 0, 0, 0, 0},

    {PROFILE_100_MS,                        "100_ms",                           0, 0, 0, 0, 0, 0},
    {PROFILE_10_MS,                         "10_ms",                            0, 0, 0, 0, 0, 0},

    {PROFILE_LOG_INFO,                      "log_info",                         0, 0, 0, 0, 0, 0},
    {PROFILE_LOG_DEBUG,                     "log_debug",                        0, 0, 0, 0, 0, 0}
};

/*******************************************************************
 * Function: ProfileEnter
 *------------------------------------------------------------------
 * Profiler, enter routine
 *------------------------------------------------------------------
 */
void ProfileEnter(uint16_t index) {
    if (index < PROFILE_COUNT) {
        Profile_tp pp = &gProfile[index];
        if (pp->entryCount != pp->exitCount) {
            pp->recursiveCount++;
        }
        pp->entryCount++;
        pp->enterTimeUsec = mlvpn_timestamp_usec();
    }
}

/*******************************************************************
 * Function: ProfileExit
 *------------------------------------------------------------------
 * Profiler, exit routine
 *------------------------------------------------------------------
 */
void ProfileExit(uint16_t index) {
    if (index < PROFILE_COUNT) {
        Profile_tp pp = &gProfile[index];
        pp->exitCount++;
        uint64_t dtime = mlvpn_timestamp_usec() - pp->enterTimeUsec;
        pp->totalTimeUsec += dtime;
        if (dtime > pp->maxTimeUsec) {
            pp->maxTimeUsec = dtime;
        }
    }
}
 
/*******************************************************************
 * Function: ProfileDump
 *------------------------------------------------------------------
 * Profiler, dump data
 *------------------------------------------------------------------
 */
void ProfileDump(char* tag) {
    char    fname[80];
    FILE*   fp = NULL;
    
    if (tag) {
        sprintf(fname, "%s_profile.txt", tag);
    } else {
        strcpy(fname, "profile.txt");
    }
    fp = fopen(fname, "wt");
    if (!fp) {
        goto errorExit;
    }
    fprintf(fp, "--NAME------------------------ --ENTRY---/--EXIT----/-RECURSION  --TOTAL-us  AVERAGE-us  ----MAX-us\n");
    for (int i = 0; i < PROFILE_COUNT; i++) {
        Profile_tp pp = &gProfile[i];
        uint64_t totalTime = 0;
        uint64_t averageTime = 0;
        if (pp->exitCount) {
            totalTime = pp->totalTimeUsec;
            averageTime = pp->totalTimeUsec / pp->exitCount;
        } else if (pp->entryCount) {
            totalTime = mlvpn_timestamp_usec() - pp->enterTimeUsec;
            averageTime = totalTime;
        }
        fprintf(
            fp, 
            "%-30s %10d/%10d/%10d  %10" PRIu64 "  %10" PRIu64 "  %10" PRIu64 "\n",
            pp->name,
            pp->entryCount,
            pp->exitCount,
            pp->recursiveCount,
            totalTime,
            averageTime,
            pp->maxTimeUsec
        );
    }
    
  errorExit:
    if (fp) {
        fclose(fp);
    }
    return;
}
 
#endif

/*******************************************************************
 * Function: mlvpnBWSet
 *------------------------------------------------------------------
 * Set ulBandwidth module
 *------------------------------------------------------------------
 */
int mlvpnBWSet(int* kbps) {
    // Fill in tunnel bandwidths appropriately
    char fname[80];
    fname[0] = 0;
    if (gServerMode) {
        sprintf(fname, "/home/ned/mlvpn/%s.conf2", tuntap.devname);
    } else {
        sprintf(fname, "/root/mlvpn/%s.conf2", tuntap.devname);
    }
    
    int fd = -1;
    if (fname[0]) {
        fd = open(fname, O_RDWR|O_CREAT|O_NONBLOCK, 0);
        if (fd < 0) {
            log_warn("ARK", "Could not access '%s'", fname);
        }
    } else {
        log_warn("ARK", "mlvpnBWSet: Could not determine server/client mode.");
    }
    
    FILE* fp = NULL;
    int dslRate = 1000;
    int lteRate = 20000;
    // Check if rates specified
    if (kbps) {
        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
            if (!strncmp(tp->name, "dsl", 3)) {
                if (kbps[0] >= 0) {
                    tp->bandwidth = kbps[0];
                }
                dslRate = tp->bandwidth;
            } else {
                if (kbps[1] >= 0) {
                    tp->bandwidth = kbps[1];
                }
                lteRate = tp->bandwidth;
            }
        }
        fp = fdopen(fd, "w");
        if (fp) {
            if (fprintf(fp, "dsl=%d\n", dslRate) < 0) {
                log_warn("ARK", "mlvpnBWSet: Could not write to '%s'.", fname);
            }
            if (fprintf(fp, "lte=%d\n", lteRate) < 0) {
                log_warn("ARK", "mlvpnBWSet: Could not write to '%s'.", fname);
            }
            log_info("ARK", "mlvpnBWSet: Wrote '%s'.", fname);
        } else {
            log_warn("ARK", "mlvpnBWSet: Could not open '%s' for writing.", fname);
        }
    } else if (fd >= 0) {
        // Attempt to read from file
        fp = fdopen(fd, "r");
        if (fp) {
            char buf[80];
            while (fgets(buf, sizeof(buf), fp)) {
                char* sp = strtok(buf, "=\r\n");
                if (sp && !strcmp(sp, "dsl")) {
                    sp = strtok(NULL, "\r\n");
                    if (sp && strlen(sp)) {
                        int rate = atoi(sp);
                        // Find DSL tunnel
                        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
                            if (!strncmp(tp->name, "dsl", 3)) {
                                tp->bandwidth = rate;
                                log_info("ARK", "Config2: Setting rate of tunnel %s to %d kbps", tp->name, rate);
                                break;
                            }
                        }
                    }
                } else if (sp && !strcmp(sp, "lte")) {
                    sp = strtok(NULL, "\r\n");
                    if (sp && strlen(sp)) {
                        int rate = atoi(sp);
                        // Find LTE tunnel
                        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
                            if (!strncmp(tp->name, "lte", 3)) {
                                tp->bandwidth = rate;
                                log_info("ARK", "Config2: Setting rate of tunnel %s to %d kbps", tp->name, rate);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    if (fp) {
        fclose(fp);
    }

    return 0;
}    
    
/*******************************************************************
 * Function: mlvpnBwStatus
 *------------------------------------------------------------------
 * Get BW status (as a JSON string).
 *------------------------------------------------------------------
 */
int mlvpnBwStatus(char* bp) {
    bp += sprintf(bp, "{");
    
    // Server Tunnels
    bp += sprintf(bp, "\"serverTunnels\":[");
    int first = TRUE;
    for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
        if (!first) {
            *bp++ = ',';
        }
        first = FALSE;
        *bp++ = '{';
        
        bp += sprintf(bp, "\"channel\":\"%s\",", tp->name);
        if (tp->status < MLVPN_AUTHOK) {
            bp += sprintf(bp, "\"runTimeSec\":0");
        } else {
            bp += sprintf(bp, "\"runTimeSec\":%d", tp->stats.totalTimeSec);
        }
        bp += sprintf(bp, ",\"sendRateMax\":%d", tp->bandwidth);
        bp += sprintf(bp, ",\"receiveMeasuredRateMaxKbps\":%d", tp->speedTest.uploadRateKbps);

        bp += sprintf(bp, ",\"dlSpeedTest\":%d", tp->speedTest.running && tp->speedTest.runningDownload);
        bp += sprintf(bp, ",\"ulSpeedTest\":%d", tp->speedTest.running && tp->speedTest.runningUpload);

        bp += sprintf(bp, ",\"sentPackets\":%d", tp->stats.totals.send.packets);
        bp += sprintf(bp, ",\"sentPacketsSec\":%d", tp->stats.lastSec.send.packets);

        bp += sprintf(bp, ",\"sentBytes\":%" PRIu64, tp->stats.totals.send.bytes);
        bp += sprintf(bp, ",\"sentBytesSec\":%d", (int)tp->stats.lastSec.send.bytes);
        
        bp += sprintf(bp, ",\"receivedPackets\":%d", tp->stats.totals.receive.packets);
        bp += sprintf(bp, ",\"receivedPacketsSec\":%d", tp->stats.lastSec.receive.packets);

        bp += sprintf(bp, ",\"receivedBytes\":%" PRIu64, tp->stats.totals.receive.bytes);
        bp += sprintf(bp, ",\"receivedBytesSec\":%d", (int)tp->stats.lastSec.receive.bytes);

        bp += sprintf(bp, ",\"receivedOutOfOrder\":%d", tp->stats.totals.receive.outOfOrder);
        bp += sprintf(bp, ",\"receivedOutOfOrderSec\":%d", tp->stats.lastSec.receive.outOfOrder);

        bp += sprintf(bp, ",\"receivedOld\":%d", tp->stats.totals.receive.old);
        bp += sprintf(bp, ",\"receivedOldSec\":%d", tp->stats.lastSec.receive.old);
        
        bp += sprintf(bp, ",\"receivedLost\":%d", tp->stats.totals.receive.lost);
        bp += sprintf(bp, ",\"receivedLostSec\":%d", tp->stats.lastSec.receive.lost);
        
        
        *bp++ = '}';
    }
    bp += sprintf(bp, "]");
    
    // Server Tap
    bp += sprintf(bp, ",\"serverTap\":{");
    bp += sprintf(bp, "\"runTimeSec\":%d", tuntap.stats.totalTimeSec);
    
    bp += sprintf(bp, ",\"sentPackets\":%d", tuntap.stats.totals.send.packets);
    bp += sprintf(bp, ",\"sentPacketsSec\":%d", tuntap.stats.lastSec.send.packets);

    bp += sprintf(bp, ",\"sentBytes\":%" PRIu64, tuntap.stats.totals.send.bytes);
    bp += sprintf(bp, ",\"sentBytesSec\":%d", (int)tuntap.stats.lastSec.send.bytes);

    bp += sprintf(bp, ",\"sentOutOfOrder\":%d", tuntap.stats.totals.send.outOfOrder);
    bp += sprintf(bp, ",\"sentOutOfOrderSec\":%d", tuntap.stats.lastSec.send.outOfOrder);

    bp += sprintf(bp, ",\"sentOld\":%d", tuntap.stats.totals.send.old);
    bp += sprintf(bp, ",\"sentOldSec\":%d", tuntap.stats.lastSec.send.old);
    
    bp += sprintf(bp, ",\"sentLost\":%d", tuntap.stats.totals.send.lost);
    bp += sprintf(bp, ",\"sentLostSec\":%d", tuntap.stats.lastSec.send.lost);
    
    bp += sprintf(bp, ",\"receivedPackets\":%d", tuntap.stats.totals.receive.packets);
    bp += sprintf(bp, ",\"receivedPacketsSec\":%d", tuntap.stats.lastSec.receive.packets);

    bp += sprintf(bp, ",\"receivedBytes\":%" PRIu64, tuntap.stats.totals.receive.bytes);
    bp += sprintf(bp, ",\"receivedBytesSec\":%d", (int)tuntap.stats.lastSec.receive.bytes);

    bp += sprintf(bp, ",\"receivedLost\":%d", tuntap.stats.totals.receive.lost);
    bp += sprintf(bp, ",\"receivedLostSec\":%d", tuntap.stats.lastSec.receive.lost);
    bp += sprintf(bp, "}");

    // Client Tunnels
    bp += sprintf(bp, ",\"clientTunnels\":[");
    first = TRUE; 
    for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
        if (!first) {
            *bp++ = ',';
        }
        first = FALSE;
        *bp++ = '{';
        
        bp += sprintf(bp, "\"channel\":\"%s\"", tp->name);
        bp += sprintf(bp, ",\"runTimeSec\":%d", tp->clientStats.totalTimeSec);
        bp += sprintf(bp, ",\"sendRateMaxKbps\":%d", tp->remoteSendMaxRateKbps);
        bp += sprintf(bp, ",\"receiveMeasuredRateMaxKbps\":%d", tp->speedTest.downloadRateKbps);

        bp += sprintf(bp, ",\"receivedPackets\":%d", tp->clientStats.totals.receive.packets);
        bp += sprintf(bp, ",\"receivedPacketsSec\":%d", tp->clientStats.lastSec.receive.packets);

        bp += sprintf(bp, ",\"receivedBytes\":%" PRIu64, tp->clientStats.totals.receive.bytes);
        bp += sprintf(bp, ",\"receivedBytesSec\":%d", (int)tp->clientStats.lastSec.receive.bytes);

        bp += sprintf(bp, ",\"receivedOutOfOrder\":%d", tp->clientStats.totals.receive.outOfOrder);
        bp += sprintf(bp, ",\"receivedOutOfOrderSec\":%d", tp->clientStats.lastSec.receive.outOfOrder);
        
        bp += sprintf(bp, ",\"receivedOld\":%d", tp->clientStats.totals.receive.old);
        bp += sprintf(bp, ",\"receivedOldSec\":%d", tp->clientStats.lastSec.receive.old);

        bp += sprintf(bp, ",\"receivedLost\":%d", tp->clientStats.totals.receive.lost);
        bp += sprintf(bp, ",\"receivedLostSec\":%d", tp->clientStats.lastSec.receive.lost);
        
        *bp++ = '}';
    }
    bp += sprintf(bp, "]");
    
    // Server channels
    bp += sprintf(bp, ",\"serverChannels\":[");
    int count = 0;
    time_t ctime = time(NULL);
    for (int i = 0; i < MLVPN_NUM_CHANNELS; i++) {
        mlvpnChannel_t* cp = &channels[i];
        
        if (!cp->initTime) {
            continue;
        }
        
        if (count > 0) {
            *bp++ = ',';
        }
        count++;
        *bp++ = '{';
          
        bp += sprintf(bp, "\"channel\":%d", i);
        bp += sprintf(bp, ",\"runTimeSec\":%d", (int)(ctime - cp->initTime));

        char* wifi;
        int wifi32;
        if (i == 0) {
            wifi = "0.0.0";
            wifi32 = 0;
        } else if (i < 20) {
            wifi = "192.168.8";
            wifi32 = 100 + i;
        } else {
            wifi = "192.168.9";
            wifi32 = 100 + i;
        }
        bp += sprintf(bp, ",\"wifiIp\":\"%s.%u\"", wifi, wifi32);

        bp += sprintf(bp, ",\"sentPackets\":%d", cp->totals.send.packets);
        bp += sprintf(bp, ",\"sentPacketsSec\":%d", cp->lastSec.send.packets);
        
        bp += sprintf(bp, ",\"sentBytes\":%" PRIu64, cp->totals.send.bytes);
        bp += sprintf(bp, ",\"sentBytesSec\":%d", (int)cp->lastSec.send.bytes);

        bp += sprintf(bp, ",\"sentTcpPackets\":%d", cp->totals.send.tcpPackets);
        bp += sprintf(bp, ",\"sentTcpPacketsSec\":%d", cp->lastSec.send.tcpPackets);
        
        bp += sprintf(bp, ",\"sentTcpBytes\":%" PRIu64, cp->totals.send.tcpBytes);
        bp += sprintf(bp, ",\"sentTcpBytesSec\":%d", (int)cp->lastSec.send.tcpBytes);

        bp += sprintf(bp, ",\"sentUdpPackets\":%d", cp->totals.send.udpPackets);
        bp += sprintf(bp, ",\"sentUdpPacketsSec\":%d", cp->lastSec.send.udpPackets);
        
        bp += sprintf(bp, ",\"sentUdpBytes\":%" PRIu64, cp->totals.send.udpBytes);
        bp += sprintf(bp, ",\"sentUdpBytesSec\":%d", (int)cp->lastSec.send.udpBytes);

        bp += sprintf(bp, ",\"receivedPackets\":%d", cp->totals.receive.packets);
        bp += sprintf(bp, ",\"receivedPacketsSec\":%d", cp->lastSec.receive.packets);
        
        bp += sprintf(bp, ",\"receivedBytes\":%" PRIu64, cp->totals.receive.bytes);
        bp += sprintf(bp, ",\"receivedBytesSec\":%d", (int)cp->lastSec.receive.bytes);

        bp += sprintf(bp, ",\"receivedTcpPackets\":%d", cp->totals.receive.tcpPackets);
        bp += sprintf(bp, ",\"receivedTcpPacketsSec\":%d", cp->lastSec.receive.tcpPackets);
        
        bp += sprintf(bp, ",\"receivedTcpBytes\":%" PRIu64, cp->totals.receive.tcpBytes);
        bp += sprintf(bp, ",\"receivedTcpBytesSec\":%d", (int)cp->lastSec.receive.tcpBytes);

        bp += sprintf(bp, ",\"receivedUdpPackets\":%d", cp->totals.receive.udpPackets);
        bp += sprintf(bp, ",\"receivedUdpPacketsSec\":%d", cp->lastSec.receive.udpPackets);
        
        bp += sprintf(bp, ",\"receivedUdpBytes\":%" PRIu64, cp->totals.receive.udpBytes);
        bp += sprintf(bp, ",\"receivedUdpBytesSec\":%d", (int)cp->lastSec.receive.udpBytes);
        
        *bp++ = '}';
    }
    bp += sprintf(bp, "]");
    
    bp += sprintf(bp, "}");
    
    return 0;
}

/*******************************************************************
 * Function: mlvpn100ms
 *------------------------------------------------------------------
 * 100 ms timeout
 *------------------------------------------------------------------
 */
static uint64_t sLastTime = 0;

int mlvpn100ms(void) {
    
    int oneSec = FALSE;
    uint64_t currentTime = mlvpn_timestamp_msec();
    if ((currentTime - sLastTime) >= 1000) {
        sLastTime = currentTime;
        oneSec = TRUE;
    }

    // Check it time to report
    if (oneSec) {
        log_info("ARK", "-------------------------------------------------------------------------------");
        uint32_t ctime = time(NULL);
        
        // Handle tap statistics
        tuntap.stats.totalTimeSec = ctime - tuntap.initTime;
        if (tuntap.stats.sendUpdated) {
            tuntap.stats.totals.send.packets += tuntap.stats.current.send.packets;
            tuntap.stats.totals.send.bytes += tuntap.stats.current.send.bytes;
            tuntap.stats.totals.send.outOfOrder += tuntap.stats.current.send.outOfOrder;
            tuntap.stats.totals.send.old += tuntap.stats.current.send.old;
            
            // Determine lost
            uint32_t count = 0;
            uint32_t savedSeqNum = 0;
            if (tuntap.sendLastSeqNum) {
                count = tuntap.sendLastSeqNum - tuntap.sendFirstSeqNum;
                count++;
                if (tuntap.sendLastSeqNum < tuntap.sendFirstSeqNum) {
                    // Skip zero
                    count--;
                }
                savedSeqNum = tuntap.sendLastSeqNum + 1;
                if (!savedSeqNum) {
                    savedSeqNum = 1;
                }
            }
            int lost = count - tuntap.stats.current.send.packets;
            if (lost < 0) {
                log_info("TAP", "Error in loss calculation=%d", lost);
                lost = 0;
            }
            tuntap.stats.totals.send.lost += lost;
            memcpy(&tuntap.stats.lastSec.send, &tuntap.stats.current.send, sizeof(tuntap.stats.lastSec.send));
            tuntap.stats.lastSec.send.lost = lost;
            //log_info("ARK", "STAP: first=%d last=%d pkts=%d lost=%d", tuntap.sendLastSeqNum, tuntap.sendFirstSeqNum, tuntap.stats.current.send.packets, lost);
            memset(&tuntap.stats.current.send, 0, sizeof(tuntap.stats.current.send));
            tuntap.sendLastSeqNum = 0;
            tuntap.sendFirstSeqNum = savedSeqNum;
            tuntap.stats.sendUpdated = FALSE;
        }
        
        if (tuntap.stats.receiveUpdated) {
            tuntap.stats.totals.receive.packets += tuntap.stats.current.receive.packets;
            tuntap.stats.totals.receive.bytes += tuntap.stats.current.receive.bytes;
            tuntap.stats.totals.receive.lost += tuntap.stats.current.receive.lost;
            
            memcpy(&tuntap.stats.lastSec.receive, &tuntap.stats.current.receive, sizeof(tuntap.stats.lastSec.receive));
            memset(&tuntap.stats.current.receive, 0, sizeof(tuntap.stats.current.receive));
            tuntap.stats.receiveUpdated = FALSE;
        }
        
        log_info(
            "TAP", "RX TAP      : Pkts=%4d Rate=%9.3f kbps Lost=%d", 
            tuntap.stats.lastSec.receive.packets, 
            (float)tuntap.stats.lastSec.receive.bytes * 8.0 / 1000.0,
            tuntap.stats.lastSec.receive.lost
        );
        
        // Handle tunnel statistics
        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
            if (tp->status < MLVPN_AUTHOK) {
                memset(&tp->stats, 0, (int)sizeof(tp->stats));
                continue;
            }
            
            tp->stats.totalTimeSec = ctime - tp->initTime;
            if (tp->stats.sendUpdated) {
                tp->stats.totals.send.packets += tuntap.stats.current.send.packets;
                tp->stats.totals.send.bytes += tuntap.stats.current.send.bytes;
                
                memcpy(&tp->stats.lastSec.send, &tp->stats.current.send, sizeof(tuntap.stats.lastSec.receive));
                memset(&tp->stats.current.send, 0, sizeof(tp->stats.current.send));
                tp->stats.sendUpdated = FALSE;
            }
                
            if (tp->stats.receiveUpdated) {
                tp->stats.totals.receive.packets += tp->stats.current.receive.packets;
                tp->stats.totals.receive.bytes += tp->stats.current.receive.bytes;
                tp->stats.totals.receive.outOfOrder += tp->stats.current.receive.outOfOrder;
                tp->stats.totals.receive.old += tp->stats.current.receive.old;
                
                // Determine lost
                uint16_t count = 0;
                uint16_t savedSeqNum = 0;
                if (tp->receiveLastSeqNum) {
                    count = tp->receiveLastSeqNum - tp->receiveFirstSeqNum;
                    count++;
                    if (tp->receiveLastSeqNum < tp->receiveFirstSeqNum) {
                        // Skip zero
                        count--;
                    }
                    savedSeqNum = tp->receiveLastSeqNum + 1;
                    if (!savedSeqNum) {
                        savedSeqNum = 1;
                    }
                }
                int lost = count - tp->stats.current.receive.packets;
                if (lost < 0) {
                    log_info("TUN", "%s Error in loss calculation=%d", tp->name, lost);
                    lost = 0;
                }
                tp->stats.totals.receive.lost += lost;
                memcpy(&tp->stats.lastSec.receive, &tp->stats.current.receive, sizeof(tp->stats.lastSec.receive));
                tp->stats.lastSec.receive.lost = lost;
                memset(&tp->stats.current.receive, 0, sizeof(tp->stats.current.receive));
                tp->receiveLastSeqNum = 0;
                tp->receiveFirstSeqNum = savedSeqNum;
                tp->stats.receiveUpdated = FALSE;
            }
            
            log_info(
                "TUN", "TX %s    : Pkts=%4d Rate=%9.3f kbps", 
                tp->name, tp->stats.lastSec.send.packets, 
                (float)tp->stats.lastSec.send.bytes * 8.0 / 1000.0
            );
        }
    
        log_info("ARK", "--------------------");
        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
            if (tp->status < MLVPN_AUTHOK) {
                continue;
            }
            log_info(
                "TUN", "RX %s %3d: Pkts=%4d Rate=%9.3f kbps OOO=%4d Old=%4d Lost=%4d", 
                tp->name, 
                tp->rtt, 
                tp->stats.lastSec.receive.packets, 
                (float)tp->stats.lastSec.receive.bytes * 8.0 / 1000.0, 
                tp->stats.lastSec.receive.outOfOrder, 
                tp->stats.lastSec.receive.old, 
                tp->stats.lastSec.receive.lost
            );
        }
        
        log_info(
            "TAP", "TX TAP      : Pkts=%4d Rate=%9.3f kbps OOO=%4d Old=%4d Lost=%4d", 
            tuntap.stats.lastSec.send.packets, 
            (float)tuntap.stats.lastSec.send.bytes * 8.0 / 1000.0, 
            tuntap.stats.lastSec.send.outOfOrder, 
            tuntap.stats.lastSec.send.old, 
            tuntap.stats.lastSec.send.lost
        );

        if (sNumFreePackets < (MLVPN_MAX_PACKETS / 10)) {
            log_info("ARK", "WARNING: Number of free packets=%d/%d", 
                sNumFreePackets, MLVPN_MAX_PACKETS);
        }
        
        if (gServerMode) {
            // Update stats for any active channels
            for (int i = 0; i < MLVPN_NUM_CHANNELS; i++) {
                mlvpnChannel_t* cp = &channels[i];
                if (!cp->initTime) {
                    continue;
                }
                
                if (cp->sendUpdated) {
                    cp->totals.send.packets += cp->current.send.packets;
                    cp->totals.send.bytes += cp->current.send.bytes;
                    cp->totals.send.tcpPackets += cp->current.send.tcpPackets;
                    cp->totals.send.tcpBytes += cp->current.send.tcpBytes;
                    cp->totals.send.udpPackets += cp->current.send.udpPackets;
                    cp->totals.send.udpBytes += cp->current.send.udpBytes;
                    
                    memcpy(&cp->lastSec.send, &cp->current.send, sizeof(cp->lastSec.send));
                    memset(&cp->current.send, 0, sizeof(cp->current.send));
                    
                    cp->sendUpdated = FALSE;
                }
                if (cp->receiveUpdated) {
                    cp->totals.receive.packets += cp->current.receive.packets;
                    cp->totals.receive.bytes += cp->current.receive.bytes;
                    cp->totals.receive.tcpPackets += cp->current.receive.tcpPackets;
                    cp->totals.receive.tcpBytes += cp->current.receive.tcpBytes;
                    cp->totals.receive.udpPackets += cp->current.receive.udpPackets;
                    cp->totals.receive.udpBytes += cp->current.receive.udpBytes;
                    
                    memcpy(&cp->lastSec.receive, &cp->current.receive, sizeof(cp->lastSec.receive));
                    memset(&cp->current.receive, 0, sizeof(cp->current.receive));
                    
                    cp->receiveUpdated = FALSE;
                }
            }
            
            if (!sSpeedTestRunning) {
                for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
                    monitor_error_rate(
                        tp,
                        FALSE,
                        mlvpn_timestamp_msec(), 
                        tp->stats.lastSec.receive.packets,
                        tp->stats.lastSec.receive.bytes,
                        tp->stats.lastSec.receive.lost
                    );
                }
            }
        } else {
            for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
                if (tp->status >= MLVPN_AUTHOK) {
                    mlvpn_rtun_send_client_stats(tp);
                }
            }
        }
    }
    
    return 0;
}

/*******************************************************************
 * Function: mlvpn10ms
 *------------------------------------------------------------------
 * 10 ms timeout
 *------------------------------------------------------------------
 */
int mlvpn10ms(void) {
    // Check for running speedtest
    //log_info("ARK", "10 ms timer");
    if (sSpeedTestRunning) {
        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
            if (((gServerMode && tp->speedTest.runningDownload) || (!gServerMode && tp->speedTest.runningUpload)) && tp->speedTest.sequenceNumber < tp->speedTest.max) {
                // Determine number of packet to send
                uint64_t currentTime = mlvpn_timestamp_msec();
                uint32_t targetNumber;
                if (!tp->speedTest.txTransmitted) {
                    log_info("speedtest", "%s mlvpn10ms: first transmit", tp->name);
                    tp->speedTest.txStartTime = currentTime;
                    targetNumber = tp->speedTest.batch;
                } else {
                    targetNumber = (currentTime - tp->speedTest.txStartTime) * tp->speedTest.batch / (SPEEDTEST_TIME_MSEC / SPEEDTEST_INTERVALS_PER_SEC); 
                }
                // Queue packets
                //log_info("ARK", "10 ms timer: %d/%d", tp->speedTest.txTransmitted, targetNumber);
                while (tp->speedTest.txTransmitted < targetNumber) {
                    if (tp->speedTest.sequenceNumber >= tp->speedTest.max) {
                        break;
                    }
                    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();;
                    if (!pp) {
                        log_warnx("speedtest", "%s high priority buffer: overflow", tp->name);
                        break;
                    } else {
                        //log_info("speedtest", "SpeedTest (%s) queuing %d.", tp->name, tp->speedTest.sequenceNumber);
                        mlvpn_pkt_speedtest_t* mp = (mlvpn_pkt_speedtest_t*)pp->data;
                        mp->testNumber = htobe16(tp->speedTest.testNumber);
                        mp->sequenceNumber = htobe32(tp->speedTest.sequenceNumber++);
                        if (tp->speedTest.sequenceNumber < tp->speedTest.max) {
                            mp->endFlag = htobe16(FALSE);
                        } else {
                            mp->endFlag = htobe16(TRUE);
                            log_info("speedtest", "%s mlvpn10ms: setting endflag txTransmitted:%d", tp->name, tp->speedTest.txTransmitted + 1);
                        }
                        pp->protocol.dataLength = tp->speedTest.length - MLVPN_PROTO_OVERHEAD;
                        pp->protocol.messageId = MLVPN_PKT_SPEEDTEST;
                        tp->speedTest.txTransmitted++;
                        mlvpn_queue_hsbuf(tp, pp);
                    }
                }           
                break;
            }
        }
    }
    return 0;
}

/*******************************************************************
 * Function: SpeedTestSetParameters
 *------------------------------------------------------------------
 * Set speed test parameters
 *------------------------------------------------------------------
 */
static int SpeedTestSetParameters(mlvpn_tunnel_t* tp, uint32_t rateKbps) {
    float testLengthSec = (SPEEDTEST_TIME_MSEC / 1000.0);
    int intervals = (int)(testLengthSec * SPEEDTEST_INTERVALS_PER_SEC);
    float bitsPerInterval = (rateKbps * 1000.0 * testLengthSec) / intervals;
    int packetsPerInterval = (int)(bitsPerInterval / SPEEDTEST_BITS_PER_PKT + 1);
    if (packetsPerInterval < 1) {
        packetsPerInterval = 1;
    }
    int packetLengthBytes = bitsPerInterval / packetsPerInterval / 8;
    if (packetLengthBytes > 1000) {
        packetLengthBytes = 1000;
    }
    tp->speedTest.batch = packetsPerInterval;
    tp->speedTest.max = packetsPerInterval * intervals;
    tp->speedTest.length = packetLengthBytes;
    tp->speedTest.currentRateKbps = packetsPerInterval * packetLengthBytes * 8 * SPEEDTEST_INTERVALS_PER_SEC / 1000;
    log_info(
        "speedtest", 
        "%s SpeedTestSetParameters rateKbps:%d batch:%d lengthBytes:%d max:%d",
        tp->name,
        rateKbps,
        packetsPerInterval,
        packetLengthBytes,
        tp->speedTest.max
    );
    return 0;
}

/*******************************************************************
 * Function: SpeedTestStart
 *------------------------------------------------------------------
 * Start speed test
 *------------------------------------------------------------------
 */
int SpeedTestStart(mlvpn_tunnel_t* tp) {
#if SPEEDTEST
    if (gServerMode && !sSpeedTestRunning && (sSpeedTestForce || mlvpn_options.initial_speedtest)) {
        // Perform speedtests on DSL first
        mlvpn_tunnel_t* tp = NULL;
        for (mlvpn_tunnel_t* tp2 = rtuns.first; tp2; tp2 = tp2->next) {
            if (tp2->status < MLVPN_AUTHOK || tp2->speedTest.running || (tp2->speedTest.downloadDone && tp2->speedTest.uploadDone)) {
                continue;
            }
            tp = tp2;
            if (!strncmp(tp2->name, "dsl", 3)) {
                break;
            }
        }
        
        if (tp) {
            if (!tp->speedTest.downloadDone) {
                SpeedTestSetParameters(tp, SPEEDTEST_INITIAL_RATE_KBPS);
                log_info("speedtest", "%s SpeedTest download start rateKbps:%d", tp->name, tp->speedTest.currentRateKbps);
                tp->speedTest.runningDownload = TRUE;
                tp->speedTest.runningUpload = FALSE;
                tp->speedTest.downloadRateKbps = 0;
                tp->speedTest.downloadTestNumber++;
                tp->speedTest.downloadTestNumberReport = tp->speedTest.downloadTestNumber - 1;
                tp->speedTest.testNumber = tp->speedTest.downloadTestNumber;
                tp->speedTest.downloadWaitingAck = FALSE;
                
                tp->speedTest.running = TRUE;
                tp->speedTest.tries = 2;
                tp->speedTest.maxGoodRateKbps = 0;
                tp->speedTest.minBadRateKbps = 0;
                tp->speedTest.txTransmitted = 0;
                tp->speedTest.sequenceNumber = 0; 
                sSpeedTestRunning = TRUE;
            } else {
                SpeedTestSetParameters(tp, SPEEDTEST_INITIAL_RATE_KBPS);
                log_info("speedtest", "%s SpeedTest upload start ratekBps:%d", tp->name, tp->speedTest.currentRateKbps);
                tp->speedTest.runningDownload = FALSE;
                tp->speedTest.runningUpload = TRUE;
                tp->speedTest.uploadRateKbps = 0;
                tp->speedTest.uploadTestNumber++;
                tp->speedTest.uploadTestNumberReport = tp->speedTest.uploadTestNumber - 1;
                tp->speedTest.testNumber = tp->speedTest.uploadTestNumber;
                
                tp->speedTest.running = TRUE;
                tp->speedTest.tries = 2;
                tp->speedTest.maxGoodRateKbps = 0;
                tp->speedTest.minBadRateKbps = 0;
                tp->speedTest.txTransmitted = 0;
                tp->speedTest.sequenceNumber = 0; 
                sSpeedTestRunning = TRUE;

                mlvpn_rtun_send_keepalive(tp);
            }
        } else {
            for (tp = rtuns.first; tp; tp = tp->next) {
                if (tp->status >= MLVPN_AUTHOK && (!tp->speedTest.downloadDone || !tp->speedTest.uploadDone)) {
                    break;
                }
            }
            if (!tp) {
                sSpeedTestForce = FALSE;
            }
        }
    } 
#endif
    return 0;
}

/*******************************************************************
 * Function: SpeedTestDone
 *------------------------------------------------------------------
 * Handle of the speed test
 *------------------------------------------------------------------
 */
int SpeedTestDone(mlvpn_tunnel_t* tp) {
    if (gServerMode) {
        if (sSpeedTestRunning && tp->speedTest.runningDownload) {
            tp->speedTest.downloadTestNumberReport = tp->speedTest.downloadTestNumber;
            tp->speedTest.downloadWaitingAck = TRUE;
            log_info("speedtest", "%s SpeedTest download done, downloadTestNumberReport:%d.", tp->name, tp->speedTest.downloadTestNumberReport);
            mlvpn_rtun_send_keepalive(tp);
        }
    } else {
        log_info("speedtest", "%s SpeedTest upload done.", tp->name);
        tp->speedTest.running = FALSE;
        tp->speedTest.runningDownload = FALSE;
        tp->speedTest.runningUpload = FALSE;
        sSpeedTestRunning = FALSE;
        tp->speedTest.uploadTestNumberReport = tp->speedTest.uploadTestNumber;
        mlvpn_rtun_send_keepalive(tp);
    }
    return 0;
}

/*******************************************************************
 * Function: SpeedTestAcked
 *------------------------------------------------------------------
 * Handle results of the speed test
 *------------------------------------------------------------------
 */
int SpeedTestAcked(mlvpn_tunnel_t* tp, uint32_t numberPackets, uint32_t numberBytes, uint16_t deltaTime) {
    if (gServerMode) {
        log_info("speedtest", "%s SpeedTestAcked()", tp->name);
        if ((tp->speedTest.runningDownload && tp->speedTest.downloadWaitingAck) || tp->speedTest.runningUpload) {
            tp->speedTest.downloadWaitingAck = FALSE;
            
            // Get speed control config
            //mlvpnBWControl_t* cp;
            
            uint32_t rate = 0;
            //int percent1 = (deltaTime * 100) / (SPEEDTEST_TIME_MSEC);
            int percent2 = (numberPackets * 100) / tp->speedTest.max;
            //if (percent1 > 95 && percent1 < 105 && percent2 > 97 && percent2 < 101) {
            if (percent2 > 97 && percent2 < 101) {
                if (deltaTime) {
                    rate = (numberBytes * 8) / deltaTime;
                }
            }
            
            int targetRate;
            if (tp->speedTest.runningDownload) {
                log_info("speedtest", "%s SpeedTest download acked, numPackets:%d/%d numberBytes:%d deltaTime:%d rate:%d/%d", tp->name, numberPackets, tp->speedTest.max, numberBytes, deltaTime, rate, tp->speedTest.downloadRateKbps);
                targetRate = tp->speedTest.downloadRateKbps;
            } else {
                log_info("speedtest", "%s SpeedTest upload acked, numPackets:%d/%d deltaTime:%d rate:%d/%d", tp->name, numberPackets, tp->speedTest.max, deltaTime, rate, tp->speedTest.uploadRateKbps);
                targetRate = tp->speedTest.uploadRateKbps;
            }
            
            int done = FALSE;
            if (!tp->speedTest.currentRateKbps) {
                done = TRUE;
            } else if (rate && rate > targetRate) {
                if (tp->speedTest.runningDownload) {
                    tp->speedTest.downloadRateKbps = rate;
                } else {
                    tp->speedTest.uploadRateKbps = rate;
                }
                
                if (rate > tp->speedTest.maxGoodRateKbps) {
                    tp->speedTest.maxGoodRateKbps = rate;
                }
                
                // Increase rate
                int newRate;
                if (tp->speedTest.minBadRateKbps) {
                    // Split the difference
                    newRate = rate + (tp->speedTest.minBadRateKbps - rate) / 2;
                } else {
                    // No known upper bound, try doubling
                    newRate = rate * 2;
                }
                
                // Check if difference large enough
                int diff = newRate - rate;
                if (diff < 0) {
                    diff *= -1;
                }
                diff = (diff * 100) / rate;
                if (diff < SPEEDTEST_RATE_DIFF_THRESHOLD || rate >= MAX_BANDWIDTH_KBPS) {
                    // Consider done
                    done = TRUE;
                } else {
                    SpeedTestSetParameters(tp, newRate);
                    log_info("speedtest", "SpeedTest (%s) restart rateKbps:%d", tp->name, tp->speedTest.currentRateKbps);
                    if (tp->speedTest.minBadRateKbps) {
                        tp->speedTest.tries = 1;
                    } else {
                        tp->speedTest.tries = 2;
                    }
                }
            } else if (--tp->speedTest.tries > 0) {
                // Check for retry
                log_info("speedtest", "SpeedTest (%s) retry rateKbps:%d", tp->name, tp->speedTest.currentRateKbps);
            } else {
                if (!tp->speedTest.minBadRateKbps || tp->speedTest.currentRateKbps < tp->speedTest.minBadRateKbps) {
                    tp->speedTest.minBadRateKbps = tp->speedTest.currentRateKbps;
                }
                 
                // Try moving down in rate
                int newRate;
                if (tp->speedTest.maxGoodRateKbps < tp->speedTest.currentRateKbps) {
                    // Split the difference
                    newRate = tp->speedTest.maxGoodRateKbps + (tp->speedTest.currentRateKbps - tp->speedTest.maxGoodRateKbps) / 2;
                } else {
                    // No known lower bound, try halving
                    newRate = tp->speedTest.currentRateKbps / 2;
                }
                
                // Check for minimum newrate
                if (newRate < 100) {
                    done = TRUE;
                } else {
                    // Check if difference large enough
                    int diff = newRate - tp->speedTest.currentRateKbps;
                    if (diff < 0) {
                        diff *= -1;
                    }
                    diff = (diff * 100) / tp->speedTest.currentRateKbps;
                    if (diff < SPEEDTEST_RATE_DIFF_THRESHOLD) {
                        // Consider done
                        done = TRUE;
                    } else {
                        SpeedTestSetParameters(tp, newRate);
                        tp->speedTest.tries = 1;

                        log_info("speedtest", "SpeedTest (%s) download rateKbps:%d", tp->name, tp->speedTest.currentRateKbps);
                    }
                }
            }
            
            if (done) {
                // Consider done
                
                // Set rate if DSL
                //if (!strncmp(tp->name, "dsl", 3)) {
                    if (tp->speedTest.runningDownload) {
                        tp->bandwidth = (tp->speedTest.downloadRateKbps * 9) / 10;
                        log_info("ARK", "SpeedTestAcked: %s: setting download sendMaxRateKbps:%d", tp->name, tp->ulBandwidth);
                    } else {
                        uint32_t rate = (tp->speedTest.uploadRateKbps * 9) / 10;
                        log_info("ARK", "SpeedTestAcked: %s: setting upload sendMaxRateKbps:%d", tp->name, rate);
                        tp->ulBandwidth = rate;
                        if (!strncmp(tp->name, "dsl", 3)) {
                            mlvpn_control_pkt_send(rate, -1, 0, 0);
                        } else {
                            mlvpn_control_pkt_send(-1, rate, 0, 0);
                        }
                    }
                //}
                
                if (tp->speedTest.runningDownload) {
                    tp->speedTest.runningDownload = FALSE;
                    tp->speedTest.downloadDone = TRUE;
                } else {
                    tp->speedTest.runningUpload = FALSE;
                    tp->speedTest.uploadDone = TRUE;
                }
                tp->speedTest.running = FALSE;
                sSpeedTestRunning = FALSE;
                                
                // Queue next test
                SpeedTestStart(tp);
            } else {
                if (tp->speedTest.runningDownload) {
                    tp->speedTest.downloadTestNumber++;
                    tp->speedTest.testNumber = tp->speedTest.downloadTestNumber;
                } else {
                    tp->speedTest.uploadTestNumber++;
                    tp->speedTest.testNumber = tp->speedTest.uploadTestNumber;
                }
                tp->speedTest.sequenceNumber = 0; 
                tp->speedTest.txTransmitted = 0;
            }
        }
    }
    return 0;
}

/*******************************************************************
 * Function: SpeedTestInitiate
 *------------------------------------------------------------------
 * Initiate a new speed test
 *------------------------------------------------------------------
 */
int SpeedTestInitiate() {
    if (gServerMode) {
        log_info("speedtest", "SpeedTestInitiate()");
        for (mlvpn_tunnel_t* tp = rtuns.first; tp; tp = tp->next) {
            tp->speedTest.downloadDone = FALSE;
            tp->speedTest.uploadDone = FALSE;
            tp->speedTest.running = FALSE;
            tp->speedTest.runningDownload = FALSE;
            tp->speedTest.runningUpload = FALSE;
        }
        sSpeedTestRunning = FALSE;
        sSpeedTestForce = TRUE;
    }
    return 0;
}

/*******************************************************************
 * Function: SpeedTestIsRunning
 *------------------------------------------------------------------
 * Check if speed test running
 *------------------------------------------------------------------
 */
int SpeedTestIsRunning() {
    return sSpeedTestRunning;
}

/*******************************************************************
 * Function: SpeedTestRunUpload
 *------------------------------------------------------------------
 * Run upload test
 *------------------------------------------------------------------
 */
int SpeedTestRunUpload(mlvpn_tunnel_t* tp, uint16_t testNumber) {
    if (!gServerMode) {
        log_info("speedtest", "%s SpeedTestRunUpload", tp->name);
        sSpeedTestRunning = TRUE;
        tp->speedTest.running = TRUE;
        tp->speedTest.runningDownload = FALSE;
        tp->speedTest.runningUpload = TRUE;
        tp->speedTest.txTransmitted = 0;
        tp->speedTest.sequenceNumber = 0;
        tp->speedTest.testNumber = testNumber;
    }
    
    return 0;
}

/*******************************************************************
 * Function: SpeedTestReset
 *------------------------------------------------------------------
 * Reset speed test for the indicated tunnel
 *------------------------------------------------------------------
 */
int SpeedTestReset(mlvpn_tunnel_t* tp) {
    PROFILE_ENTER(PROFILE_SPEEDTEST_RESET);
    
    memset(&tp->speedTest, 0, sizeof(tp->speedTest));
    
    // Check if need to clear global flag
    sSpeedTestRunning = FALSE;
    for (mlvpn_tunnel_t* tp2 = rtuns.first; tp2; tp2 = tp2->next) {
        if (tp2->speedTest.running) {
            sSpeedTestRunning = TRUE;
            break;
        }
    }
    
    PROFILE_EXIT(PROFILE_SPEEDTEST_RESET);
    return 0;
}

/*******************************************************************
 * Function: MlvpnPacketsAllocate
 *------------------------------------------------------------------
 * Allocate packet buffers
 *------------------------------------------------------------------
 */
int MlvpnPacketsAllocate() {
    int errCode = ERR_CODE_ERROR;
    
    for (int i = 0; i < MLVPN_MAX_PACKETS; i++) {
        mlvpn_pkt_t* pp = malloc(sizeof(*pp));
        if (!pp) {
            log_warn("ARK", "MlvpnPacketsAllocate: Could only allocate %d packets.", i);
            goto errorExit;
        }
        if (MlvpnPacketsReturnFreePacket(pp)) {
            goto errorExit;
        }
    }
    errCode = ERR_CODE_OK;
    
  errorExit:
    return errCode;
}

/*******************************************************************
 * Function: MlvpnPacketsGetNextFree
 *------------------------------------------------------------------
 * Get next free packet buffer
 *------------------------------------------------------------------
 */
mlvpn_pkt_t* MlvpnPacketsGetNextFree() {
    mlvpn_pkt_t* pp = NULL;
    
    if (sFreePacketsFirst) {
        pp = sFreePacketsFirst;
        sFreePacketsFirst = sFreePacketsFirst->next;
        if (sFreePacketsFirst) {
            sFreePacketsFirst->previous = NULL;
        } else {
            sFreePacketsLast = NULL;
        }
        pp->next = pp->previous = NULL;
        sNumFreePackets--;
    } else {
        //log_warnx("ARK", "MlvpnPacketsGetNextFree: No free packets.");
    }
    
    return pp;
}

/*******************************************************************
 * Function: MlvpnPacketsReturnFreePacket
 *------------------------------------------------------------------
 * Return a packet to the free list
 *------------------------------------------------------------------
 */
int MlvpnPacketsReturnFreePacket(mlvpn_pkt_t* pp) {
    int errCode = ERR_CODE_ERROR;
    
    if (!pp) {
        log_warn("ARK", "MlvpnPacketsReturnFreePacket: Returning NULL packet.");
        goto errorExit;
    }
    
    pp->previous = sFreePacketsLast;
    pp->next = NULL;
    if (sFreePacketsLast) {
        sFreePacketsLast->next = pp;
    } else {
        sFreePacketsFirst = pp;
    }
    sFreePacketsLast = pp;
    sNumFreePackets++;
    errCode = ERR_CODE_OK;
    
  errorExit:
    return errCode;
}

/*******************************************************************
 * Function: mystrlcpy
 *------------------------------------------------------------------
 * Safe strcpy
 *------------------------------------------------------------------
 */
#pragma GCC diagnostic ignored "-Wstringop-overflow"
size_t mystrlcpy(char *dst, const char *src, size_t size) {
    size_t len = 0;
    
    if (!dst || !src || !size) {
        goto errorExit;
    }
    
    size_t slen = strlen(src);
    if (slen > (size - 1)) {
        slen = size - 1;
    }
    strncpy(dst, src, slen);
    dst[slen] = 0;
    len = slen;
    
  errorExit:
    return len;
}

/*******************************************************************
 * Function: mystrlcat
 *------------------------------------------------------------------
 * Safe strcat
 *------------------------------------------------------------------
 */
size_t mystrlcat(char *dst, const char *src, size_t size) {
    size_t len = 0;
    
    if (!dst || !src || !size) {
        goto errorExit;
    }
    
    size_t dlen = strlen(dst);
    size_t slen = strlen(src);
    size_t tlen = dlen + slen;
    if (tlen > (size - 1)) {
        tlen = size - 1;
    }
    if (tlen <= dlen) {
        goto errorExit;
    }
    slen = tlen - dlen;
    strncpy(dst + dlen, src, slen);
    dst[tlen] = 0;
    len = tlen;
    
  errorExit:
    return len;
}

/********************************************************************
 * Function: mlvpn_clear_tunnel_stats
 *-------------------------------------------------------------------
 * Clear tunnel stats
 *-------------------------------------------------------------------
 */
int mlvpn_clear_tunnel_stats(mlvpn_tunnel_t* tp) {
    if (tp) {
        memset(&tp->stats, 0, (int)sizeof(tp->stats));
        memset(&tp->clientStats, 0, (int)sizeof(tp->clientStats));
    }
    return ERR_CODE_OK;
}

/********************************************************************
 * Function: monitor_error_rate
 *-------------------------------------------------------------------
 * Handle error rate
 *-------------------------------------------------------------------
 */

#define RX_ERRORS_THRESHOLD_PERCENT         (3)
#define RX_ERRORS_MIN_PACKETS_ERROR_RATE    (10)
#define RX_ERRORS_MIN_PACKETS_LTE_UPGRADE   (100)
#define RX_ERRORS_DOWN_PERCENT_DIVIDER      (10)
#define RX_ERRORS_UP_PERCENT_DIVIDER        (10)
#define RX_ERROR_TIME_DELAY_MSEC            (30000)

void monitor_error_rate(
    mlvpn_tunnel_t*     tp,
    int                 download,
    uint64_t            dataTimestampMsec, 
    uint32_t            packets,
    uint32_t            bytes,
    uint32_t            lost
) {
    if (!mlvpn_options.autospeed) {
        return;
    }
    
    // Get error structure to use
    rx_errors_t* ep = download ? &tp->dlErrors : &tp->ulErrors;
    
    // Check if new data
    if (dataTimestampMsec == ep->lastTime) {
        return;
    }
    ep->lastTime = dataTimestampMsec;
    
    // Add to error bin
    rx_errors_bin_t* rp = &ep->bins[ep->last++];
    if (ep->last >= RX_ERRORS_BINS) {
        ep->last = 0;
    }
    
    // Remove from running totals
    ep->totalPackets -= rp->packets;
    ep->totalBytes -= rp->bytes;
    ep->totalErrors -= rp->errors;
    
    // Save new data
    rp->packets = packets;
    rp->bytes = bytes;
    rp->errors = lost;
    
    // Add new data to running totals
    ep->totalPackets += rp->packets;
    ep->totalBytes += rp->bytes;
    ep->totalErrors += rp->errors;
    
    ep->count++;
    if (ep->count > RX_ERRORS_BINS) {
        ep->count = RX_ERRORS_BINS;
    }
    
    // Check to see if sufficient data received to be meaninful sample
    int origRate = download ? tp->bandwidth : tp->ulBandwidth;
    if (packets && (bytes > ((origRate * 1000) / 16) || lost >= RX_ERRORS_MIN_PACKETS_ERROR_RATE)) {
        // Set new rate to current rate
        int newRate = origRate;
        
        // Calculate and check error rate
        int errorRate = (lost * 100) / packets;
        if (errorRate > RX_ERRORS_THRESHOLD_PERCENT) {
            if (ep->count >= 1) {
                // Decrease bandwidth 
                newRate = origRate - (origRate / RX_ERRORS_DOWN_PERCENT_DIVIDER);
                ep->lastTimeDown = dataTimestampMsec;
            }
        } else if (ep->count >= RX_ERRORS_BINS && dataTimestampMsec > (ep->lastTimeDown + RX_ERROR_TIME_DELAY_MSEC)) {
            // Calculate and check error rate
            errorRate = (ep->totalErrors * 100) / ep->totalPackets;
            if (errorRate < RX_ERRORS_THRESHOLD_PERCENT) {
                int upgrade = FALSE;
                if (download) {
                    if (tp == lteTunnel) {
                        // If LTE tunnel, increase bandwidth if data queued
                        if (mlvpn_queue_count(&tuntap.rbuf) > (MLVPN_MAX_RTUN_QUEUE / 2)) {
                            upgrade = TRUE;
                        }
                    } else if (tp == dslTunnel) {
                        // If DSL tunnel and no LTE and data queued OR sending data on LTE, increase bandwith
                        if ((!lteTunnel && mlvpn_queue_count(&tuntap.rbuf) > (MLVPN_MAX_RTUN_QUEUE / 2)) 
                            || lteTunnel->stats.lastSec.send.packets > RX_ERRORS_MIN_PACKETS_LTE_UPGRADE) {
                            upgrade = TRUE;
                        }
                    }
                } else {
                    if (tp == lteTunnel) {
                        // If LTE tunnel, increase bandwidth if data queued
                        if (tp->clientTapReceiveQueue > (MLVPN_MAX_RTUN_QUEUE / 2)) {
                            upgrade = TRUE;
                        }
                    } else if (tp == dslTunnel) {
                        // If DSL tunnel and no LTE and data queued OR receiving data on LTE, increase bandwith
                        if ((!lteTunnel && mlvpn_queue_count(&tuntap.rbuf) > (MLVPN_MAX_RTUN_QUEUE / 2)) || lteTunnel->stats.lastSec.receive.packets > RX_ERRORS_MIN_PACKETS_LTE_UPGRADE) {
                            upgrade = TRUE;
                        }
                    }
                }
                if (upgrade) {
                    newRate = origRate + (origRate / RX_ERRORS_UP_PERCENT_DIVIDER);
                    if (ep->lastTimeDown) {
                        ep->lastTimeDown = dataTimestampMsec;
                    }
               }
            }
        }

        if (newRate > MAX_BANDWIDTH_KBPS) {
            newRate = MAX_BANDWIDTH_KBPS;
        }
        
        if (newRate != origRate) {
            uint64_t lastTimeDown = ep->lastTimeDown;
            memset(ep, 0, (int)sizeof(*ep));
            ep->lastTimeDown = lastTimeDown;
            
            if (download) {
                tp->bandwidth = newRate;
            } else {
                tp->ulBandwidth = newRate;
                if (!strncmp(tp->name, "dsl", 3)) {
                    mlvpn_control_pkt_send(newRate, -1, 0, 0);
                } else {
                    mlvpn_control_pkt_send(-1, newRate, 0, 0);
                }
            }
        }
    } else {
        // Not enough packets, reset sequence
        ep->count = 0;
    }
    
    return;
}
