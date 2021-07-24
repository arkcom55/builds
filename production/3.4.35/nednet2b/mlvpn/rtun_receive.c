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

void monitor_dlerror_rate(
    mlvpn_tunnel_t*     tp,
    uint64_t            dataTimestampMsec, 
    uint32_t            packets,
    uint32_t            bytes,
    uint32_t            lost
);

/********************************************************************
 * Function: mlvpn_rtun_read
 *-------------------------------------------------------------------
 * Process packet received on a tunnel
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_read(EV_P_ ev_io *w, int revents) {
    PROFILE_ENTER(PROFILE_RTUN_READ);
    
    mlvpn_pkt_t* pp = NULL;
    
    // Get tunnel
    mlvpn_tunnel_t* tp = w->data;

    int count = 0;
    while (TRUE) {
        if (++count > 2) {
            break;
        }
        
        // Get packet buffer
        if (!pp) {
            pp = MlvpnPacketsGetNextFree();
            if (!pp) {
                // Dummy read
                struct sockaddr_storage clientaddr;
                socklen_t addrlen = sizeof(clientaddr);
                char data[MLVPM_PKT_MAX_SIZE];
                PROFILE_ENTER(PROFILE_RTUN_READ_RECVFROM);
                recvfrom(
                    tp->fd, 
                    data,
                    MLVPM_PKT_MAX_SIZE,
                    MSG_DONTWAIT, 
                    (struct sockaddr *)&clientaddr, 
                    &addrlen
                );        
                PROFILE_EXIT(PROFILE_RTUN_READ_RECVFROM);
                goto errorExit;
            }
        }
        
        // Read data
        ssize_t len;
        struct sockaddr_storage clientaddr;
        socklen_t addrlen = sizeof(clientaddr);
        PROFILE_ENTER(PROFILE_RTUN_READ_RECVFROM);
        len = recvfrom(
            tp->fd, 
            &pp->protocol,
            MLVPM_PKT_MAX_SIZE,
            MSG_DONTWAIT, 
            (struct sockaddr *)&clientaddr, 
            &addrlen
        );
        PROFILE_EXIT(PROFILE_RTUN_READ_RECVFROM);
        
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                log_warn("net", "%s read error", tp->name);
                mlvpn_rtun_status_down(tp);
            }
            break;
            //log_info("protocol", "%s mlvpn_rtun_read: errno:%d", tp->name, errno);
        } else if (len == 0) {
            log_info("protocol", "%s peer closed the connection", tp->name);
            break;
        } else {
            pp->packetLength = len;

            // Validate the received packet
            if (mlvpn_protocol_read(tp, pp)) {
                goto errorExit;
            }

            // Add to tunnel statistics
            tp->stats.receiveUpdated = TRUE;
            tp->stats.current.receive.packets++;
            tp->stats.current.receive.bytes += len;

            char* status = "";
            if (!tp->expected_receiver_seq) {
                // Skip sequence number zero
                tp->expected_receiver_seq = 1;
            }
            uint16_t expected = tp->expected_receiver_seq;
            uint16_t seq = pp->protocol.tunnelSeqNum;
            if (seq) {
                uint16_t diff = seq - expected;
                if (seq != expected) {
                    if (diff <= 0x7FFF) {
                        status = "out-of-order";
                        tp->expected_receiver_seq = seq + 1;
                        tp->stats.current.receive.outOfOrder++;
                    } else {
                        status = "old";
                        tp->stats.current.receive.old++;
                    }
                } else {
                    tp->expected_receiver_seq = seq + 1;
                }
                
                // See if new sequence number for lost detection
                if (!tp->receiveFirstSeqNum) {
                    tp->receiveFirstSeqNum = seq;
                } else {
                    diff = seq - tp->receiveFirstSeqNum;
                    if (diff > 0x7FFF) {
                        tp->receiveFirstSeqNum = seq;
                    }
                }
                if (!tp->receiveLastSeqNum) {
                    tp->receiveLastSeqNum = seq;
                } else {
                    diff = seq - tp->receiveLastSeqNum;
                    if (diff <= 0x7FFF) {
                        tp->receiveLastSeqNum = seq;
                    }
                }

                log_debug(
                    "TUN", 
                    "[RECV %5s %4d] %5d/%5d %9d R:%d %s", 
                    tp->name, 
                    (int)len, 
                    seq, 
                    expected, 
                    pp->protocol.dataSeqNum, 
                    pp->protocol.flags & MLVPN_FLAGS_REORDER, 
                    status
                );
            } else {
                // Zero sequence, should be new tunnel
                log_info("ARK", "%s NEW tunnel", tp->name);
                tp->expected_receiver_seq = 1;
            }

            if (!tp->addrinfo) {
                fatalx("tp->addrinfo is NULL!");
            }

            if ((tp->addrinfo->ai_addrlen != addrlen) ||
                    (memcmp(tp->addrinfo->ai_addr, &clientaddr, addrlen) != 0)) {
                //if (mlvpn_options.cleartext_data && tp->status >= MLVPN_AUTHOK) {
                if (tp->status >= MLVPN_AUTHOK) {
                    log_warnx("protocol", "%s rejected non authenticated connection",
                        tp->name);
                    goto errorExit;
                }
                char clienthost[NI_MAXHOST];
                char clientport[NI_MAXSERV];
                int ret;
                if ( (ret = getnameinfo((struct sockaddr *)&clientaddr, addrlen,
                                        clienthost, sizeof(clienthost),
                                        clientport, sizeof(clientport),
                                        NI_NUMERICHOST|NI_NUMERICSERV)) < 0) {
                    log_warn("protocol", "%s error in getnameinfo: %d",
                           tp->name, ret);
                } else {
                    log_info("protocol", "%s new connection -> %s:%s",
                       tp->name, clienthost, clientport);
                    memcpy(tp->addrinfo->ai_addr, &clientaddr, addrlen);
                    
                    // Queue speedtest
                    memset(&tp->speedTest, 0, sizeof(tp->speedTest));
                }
            }
            //log_debug("net", "< %s recv %d bytes (type=%d, seq=%"PRIu64", reorder=%d)",
            //    tp->name, (int)len, decap_pkt.type, decap_pkt.seq, decap_pkt.reorder);

            /*******************************************************
             * Process message
             *------------------------------------------------------
             */
            if (pp->protocol.messageId == MLVPN_PKT_DATA) {
                /*******************************************************
                 * Data packet
                 *------------------------------------------------------
                 */
                if (tp->status >= MLVPN_AUTHOK) {
                    if (mlvpn_rtun_recv_data(tp, pp)) {
                        goto errorExit;
                    }
                    pp = NULL;
                } else {
                    log_debug("protocol", "%s ignoring non authenticated packet", tp->name);
                }
            } else if (pp->protocol.messageId == MLVPN_PKT_KEEPALIVE) {
                /*******************************************************
                 * Keep Alive packet
                 *------------------------------------------------------
                 */
                if (tp->status >= MLVPN_AUTHOK) {
                    tp->timeoutRunning = tp->timeout;
                    //tp->last_keepalive_ack = ev_now(EV_DEFAULT_UC);
                    
                    mlvpn_pkt_keepalive_t* mp = (mlvpn_pkt_keepalive_t*)pp->data;
                    uint64_t timestamp = be64toh(mp->rttTimestampMsec);
                    uint16_t downloadTestNumber = be16toh(mp->speedTest.downloadTestNumber);
                    uint16_t uploadTestNumber = be16toh(mp->speedTest.uploadTestNumber);
                    uint16_t uploadBatch = be16toh(mp->speedTest.uploadBatch);
                    uint32_t uploadMax = be32toh(mp->speedTest.uploadMax);
                    uint16_t uploadLength = be16toh(mp->speedTest.uploadLength);
                    
                    log_debug("protocol", "%s Rx KeepAlive, downloadTestNumber:#%d uploadTestNumber:#%d", tp->name, downloadTestNumber, uploadTestNumber);

                    if (!gServerMode) {
                        // Handle download speed test
                        tp->speedTest.downloadTestNumber = downloadTestNumber;
                        
                        // Client mode, check if upload speed test indicated
                        if (uploadTestNumber && uploadTestNumber != tp->speedTest.uploadTestNumber) {
                            tp->speedTest.uploadTestNumber = uploadTestNumber;
                            tp->speedTest.batch = uploadBatch;
                            tp->speedTest.max = uploadMax;
                            tp->speedTest.length = uploadLength;
                            log_debug("speedtest", "%s uploadTestNumber:%d batch:%d max:%d length:%d", tp->name, uploadTestNumber, uploadBatch, uploadMax, uploadLength);
                            SpeedTestRunUpload(tp, uploadTestNumber);
                        }
                    } else {
                        // Server mode, see if indicates upload speed test competed
                        if (SpeedTestIsRunning() && tp->speedTest.runningUpload && uploadTestNumber == tp->speedTest.uploadTestNumber) {
                            uint16_t deltaTime = (uint16_t)(tp->speedTest.rxLastTime - tp->speedTest.rxStartTime);
                            SpeedTestAcked(tp, tp->speedTest.rxNumberPackets, tp->speedTest.rxNumberBytes, deltaTime);
                        }
                    }
                    
                    // Echo timestamp
                    if (!gServerMode) {
                        mlvpn_rtun_send_keepalive_response(tp, timestamp);
                    }
                }
            } else if (pp->protocol.messageId == MLVPN_PKT_KEEPALIVE_RESPONSE) {
                /*******************************************************
                 * Keep Alive Response packet
                 *------------------------------------------------------
                 */
                if (tp->status >= MLVPN_AUTHOK) {
                    tp->timeoutRunning = tp->timeout;
                    //tp->last_keepalive_ack = ev_now(EV_DEFAULT_UC);
                    
                    mlvpn_pkt_keepalive_response_t* mp = (mlvpn_pkt_keepalive_response_t*)pp->data;
                    uint64_t rttTimestampMsec = be64toh(mp->rttTimestampMsec);
                    uint64_t current = mlvpn_timestamp_msec();
                    uint16_t deltaTime = (uint16_t)(current - rttTimestampMsec);
                    uint16_t downloadTestNumber = be16toh(mp->speedTest.downloadTestNumber);
                    uint16_t uploadTestNumber = be16toh(mp->speedTest.uploadTestNumber);
                       
                    tp->rtt = deltaTime;
                    log_debug("protocol", "%s Rx KeepAlive_Response (%d ms RTT), downloadTestNumber:#%d uploadTestNumber:#%d", tp->name, deltaTime, downloadTestNumber, uploadTestNumber);
                    
                    //log_info("TUN", "%s RECV lost=%d/%d", tp->name, mp->receivedLostSec, mp->receivedPacketsSec);
                    if (SpeedTestIsRunning() && tp->speedTest.runningDownload && downloadTestNumber == tp->speedTest.downloadTestNumber && tp->speedTest.downloadWaitingAck) {
                        uint32_t numberPackets = be32toh(mp->speedTest.downloadNumberPackets);
                        uint32_t numberBytes = be32toh(mp->speedTest.downloadNumberBytes);
                        uint16_t deltaTime = be16toh(mp->speedTest.downloadDeltaTime);
                        SpeedTestAcked(tp, numberPackets, numberBytes, deltaTime);
                    }
                    
                    // see if indicates upload speed test competed
                    if (SpeedTestIsRunning() && tp->speedTest.runningUpload && uploadTestNumber == tp->speedTest.uploadTestNumber) {
                        uint16_t deltaTime = (uint16_t)(tp->speedTest.rxLastTime - tp->speedTest.rxStartTime);
                        SpeedTestAcked(tp, tp->speedTest.rxNumberPackets, tp->speedTest.rxNumberBytes, deltaTime);
                    }
                    
                    SpeedTestStart(tp);
                }
            } else if (pp->protocol.messageId == MLVPN_PKT_CLIENT_STATS) {
                /*******************************************************
                 * Client Stats packet
                 *------------------------------------------------------
                 */
                if (tp->status >= MLVPN_AUTHOK) {
                    mlvpn_pkt_client_stats_t* mp = (mlvpn_pkt_client_stats_t*)pp->data;
                    mp->dataTimestampMsec = be64toh(mp->dataTimestampMsec);
                    mp->sendMaxRateKbps = be32toh(mp->sendMaxRateKbps);
                    tp->ulBandwidth = mp->sendMaxRateKbps;
                    tp->remoteSendMaxRateKbps = mp->sendMaxRateKbps;
                    tp->clientTapReceiveQueue = be16toh(mp->tapReceiveQueue);
                    
                    tp->clientStats.totals.receive.packets = be32toh(mp->receivedPackets);
                    tp->clientStats.lastSec.receive.packets = be32toh(mp->receivedPacketsSec);
                    
                    tp->clientStats.totals.receive.bytes = be32toh(mp->receivedBytes);
                    tp->clientStats.lastSec.receive.bytes = be32toh(mp->receivedBytesSec);
                    
                    tp->clientStats.totals.receive.outOfOrder = be32toh(mp->receivedOutOfOrder);
                    tp->clientStats.lastSec.receive.outOfOrder = be32toh(mp->receivedOutOfOrderSec);
                    
                    tp->clientStats.totals.receive.old = be32toh(mp->receivedOld);
                    tp->clientStats.lastSec.receive.old = be32toh(mp->receivedOldSec);
                    
                    tp->clientStats.totals.receive.lost = be32toh(mp->receivedLost);
                    tp->clientStats.lastSec.receive.lost = be32toh(mp->receivedLostSec);
                
                    if (!SpeedTestIsRunning()) {
                        monitor_error_rate(
                            tp,
                            TRUE,
                            mp->dataTimestampMsec, 
                            tp->clientStats.lastSec.receive.packets,
                            tp->clientStats.lastSec.receive.bytes,
                            tp->clientStats.lastSec.receive.lost
                        );
                    } else {
                        memset(&tp->dlErrors, 0, (int)sizeof(&tp->dlErrors));
                    }
                }
            } else if (pp->protocol.messageId == MLVPN_PKT_DISCONNECT) {
                /*******************************************************
                 * Disconnect packet
                 *------------------------------------------------------
                 */
                if (tp->status >= MLVPN_AUTHOK) {
                    log_info("protocol", "%s disconnect received", tp->name);
                    mlvpn_rtun_status_down(tp);
                }
            } else if (pp->protocol.messageId == MLVPN_PKT_AUTH || pp->protocol.messageId == MLVPN_PKT_AUTH_OK) {
                /*******************************************************
                 * AUTH or AUTH_OK packets
                 *------------------------------------------------------
                 */
                if (pp->protocol.messageId == MLVPN_PKT_AUTH) {
                    log_info("protocol", "%s received MLVPN_PKT_AUTH", tp->name);
                } else {
                    log_info("protocol", "%s received MLVPN_PKT_AUTH_OK", tp->name);
                }
                mlvpn_rtun_send_auth(tp);
            } else if (pp->protocol.messageId == MLVPN_PKT_CONTROL) {
                /*******************************************************
                 * Control packet
                 *------------------------------------------------------
                 */
                log_info("ARK", "Received BW control message");
                mlvpn_pkt_control_t* mp = (mlvpn_pkt_control_t*)pp->data;
                int32_t t1up = be32toh(mp->tun1_upload_kbps);
                int32_t t2up = be32toh(mp->tun2_upload_kbps);
                uint16_t reorder = be16toh(mp->reorder_timeout_ms);
                uint16_t stats = be16toh(mp->stats_timeout_ms);
                log_info(
                    "ARK", "Control packet: t1up=%d t2up=%d reorder=%d stats=%d",
                    t1up, t2up, reorder, stats
                );
                int kbps[2];
                kbps[0] = t1up;
                kbps[1] = t2up;
                mlvpnBWSet(kbps);
            } else if (pp->protocol.messageId == MLVPN_PKT_SPEEDTEST) {
                /*******************************************************
                 * Speed test packet
                 *------------------------------------------------------
                 */
                mlvpn_pkt_speedtest_t* mp = (mlvpn_pkt_speedtest_t*)pp->data;
                uint16_t testNumber = be16toh(mp->testNumber); 
                //uint32_t sequenceNumber = be32toh(mp->sequenceNumber); 

                // Check for speed test start
                if (testNumber != tp->speedTest.rxTestNumber) {
                    log_info("speedtest", "(%s) SpeedTest Receive Start #%d", tp->name, testNumber);
                    tp->speedTest.rxTestNumber = testNumber;
                    tp->speedTest.rxStartTime = mlvpn_timestamp_msec();
                    tp->speedTest.rxLastTime = mlvpn_timestamp_msec();
                    tp->speedTest.rxNumberPackets = 1;
                    tp->speedTest.rxNumberBytes = len;
                } else {
                    tp->speedTest.rxLastTime = mlvpn_timestamp_msec();
                    tp->speedTest.rxNumberPackets++;
                    tp->speedTest.rxNumberBytes += len;
                }
            } else {
                /*******************************************************
                 * Unknown packet
                 *------------------------------------------------------
                 */
                log_info("ARK", "Received UNKNOWN message=%d", pp->protocol.messageId);
            }
        }
    }
        
  errorExit:
    if (pp) {
        MlvpnPacketsReturnFreePacket(pp);
    }
    PROFILE_EXIT(PROFILE_RTUN_READ);
}

/********************************************************************
 * Function: mlvpn_protocol_read
 *-------------------------------------------------------------------
 * Parse the protocol header of received tunnel packet
 *-------------------------------------------------------------------
 */
int mlvpn_protocol_read(
    mlvpn_tunnel_t* tp,         // Pointer to tunnel
    mlvpn_pkt_t* pp             // Pointer to incoming packet
) {
    PROFILE_ENTER(PROFILE_PROTOCOL_READ);
    
    int errCode = ERR_CODE_ERROR;

    // Check for at least protocol data
    if (pp->packetLength < MLVPN_PROTO_OVERHEAD) {
        log_warnx("ARK", "mlvpn_protocol_read: Packet smaller than protocol header");
        goto errorExit;
    }
    
    // Decode and check message Id
    uint16_t messageId = be16toh(pp->protocol.messageId);
    if (messageId < MLVPN_FIRST_MESSAGE_ID || messageId > MLVPN_LAST_MESSAGE_ID) {
        log_warnx("ARK", "mlvpn_protocol_read: Invalid message id 0x%04X", messageId);
        goto errorExit;
    }
    pp->protocol.messageId = messageId;
    
    // Decode remaining protocol entries
    pp->protocol.mlvpnSignature = be32toh(pp->protocol.mlvpnSignature);
    pp->protocol.tunnelSeqNum = be16toh(pp->protocol.tunnelSeqNum);
    pp->protocol.dataSeqNum = be32toh(pp->protocol.dataSeqNum);
    pp->protocol.reorderSeqNum = be32toh(pp->protocol.reorderSeqNum);
    pp->protocol.dataLength = pp->packetLength - MLVPN_PROTO_OVERHEAD;
    
    errCode = ERR_CODE_OK;
    
  errorExit:
    PROFILE_EXIT(PROFILE_PROTOCOL_READ);
    return errCode;
}

/********************************************************************
 * Function: mlvpn_rtun_recv_data
 *-------------------------------------------------------------------
 * Handle data packet received on a tunnel, reorder for output
 *-------------------------------------------------------------------
 */
int mlvpn_rtun_recv_data(mlvpn_tunnel_t* tun, mlvpn_pkt_t* pp) {
    PROFILE_ENTER(PROFILE_RTUN_RECV_DATA);
    
    int errCode = ERR_CODE_ERROR;
    
    if (mlvpn_reorder(pp)) {
            goto errorExit;
    }
    
    errCode = ERR_CODE_OK;
    
  errorExit:
    PROFILE_EXIT(PROFILE_RTUN_RECV_DATA);
    return errCode;
}

/********************************************************************
 * Function: mlvpn_rtun_reorder_drain_timeout
 *-------------------------------------------------------------------
 * Timeout to drain reorder buffer
 *-------------------------------------------------------------------
 */
/*
void mlvpn_rtun_reorder_drain_timeout(EV_P_ ev_timer *w, int revents) {
    log_warnx("ARK", "REORDER TIMEOUT");
    mlvpn_reorder_flush();
}

void mlvpn_rtun_reorder_drain_timeout_stop() {
    log_debug("ARK", "mlvpn_rtun_reorder_drain_timeout_stop()");
    ev_timer_stop(EV_A_ &reorder_drain_timeout);
}

void mlvpn_rtun_reorder_drain_timeout_start() {
    //log_info("ARK", "mlvpn_rtun_reorder_drain_timeout_start()");
    ev_timer_start(EV_A_ &reorder_drain_timeout);
}

void mlvpn_rtun_reorder_drain_timeout_restart() {
    //log_info("ARK", "mlvpn_rtun_reorder_drain_timeout_restart()");
    ev_timer_again(EV_A_ &reorder_drain_timeout);
}
*/
