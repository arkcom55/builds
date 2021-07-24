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

/********************************************************************
 * Function: mlvpn_rtun_choose
 *-------------------------------------------------------------------
 * Select first available tunnel
 *-------------------------------------------------------------------
 */
mlvpn_tunnel_t* mlvpn_rtun_choose() {
    mlvpn_tunnel_t* tp = NULL;
    
    for (tp = rtuns.first; tp; tp = tp->next) {
        if (tp->status == MLVPN_AUTHOK) {
            break;
        }
    }
    
    return tp;
}

/********************************************************************
 * Function: mlvpn_rtun_inject_tuntap
 *-------------------------------------------------------------------
 * Inject the packet to the tuntap device (real network)
 *-------------------------------------------------------------------
 */
int mlvpn_rtun_inject_tuntap(mlvpn_pkt_t* pp) {
    PROFILE_ENTER(PROFILE_INJECT_TUNTAP);
    
    int errCode = ERR_CODE_ERROR;
    
    // Add to tuntap send buffer
    mlvpn_queue_put(&tuntap.sbuf, pp);
    
    // Send the packet back onto the LAN
    if (!ev_is_active(&tuntap.io_write)) {
        ev_io_start(EV_A_ &tuntap.io_write);
    }
    errCode = ERR_CODE_OK;
    
    PROFILE_EXIT(PROFILE_INJECT_TUNTAP);
    return errCode;
}

/********************************************************************
 * Function: mlvpn_rtun_send
 *-------------------------------------------------------------------
 * Prepare packet to be sent on tunnel
 *-------------------------------------------------------------------
 */
#if 0
int mlvpn_rtun_send(mlvpn_tunnel_t* tp) {
    PROFILE_ENTER(PROFILE_RTUN_SEND);
    
    int errCode = ERR_CODE_ERROR;
    mlvpn_pkt_t* pp = NULL;
    
    if (!tp) {
        goto errorExit;
    }
    
    // Check if anything to send
    pp = mlvpn_queue_get(&tp->hsbuf);
    if (pp) {
        mlvpn_rtun_send_pkt(tp, pp);
    }
    errCode = ERR_CODE_OK;
    
  errorExit:
    PROFILE_EXIT(PROFILE_RTUN_SEND);
    return errCode;
}
#endif

/********************************************************************
 * Function: mlvpn_rtun_send_pkt
 *-------------------------------------------------------------------
 * Prepare packet to be sent on tunnel
 *-------------------------------------------------------------------
 */
int mlvpn_rtun_send_pkt(mlvpn_tunnel_t* tp, mlvpn_pkt_t* pp) {
    PROFILE_ENTER(PROFILE_RTUN_SEND_PKT);
    
    int errCode = ERR_CODE_ERROR;
 
    // Check if running speed test
    if (tp->speedTest.running) {
        // Check for speed test packet
        if (pp->protocol.messageId == MLVPN_PKT_SPEEDTEST) {
            // Check for last packet of speed test
            mlvpn_pkt_speedtest_t* mp = (mlvpn_pkt_speedtest_t*)pp->data;
            if (mp->endFlag) {
                SpeedTestDone(tp);
            }
        }
    }
    
    // Format protocol info
    uint16_t length = pp->protocol.dataLength + MLVPN_PROTO_OVERHEAD;
    log_debug(
        "TUN", 
        "[SEND %5s %4d] %5d %9d R:%d", 
        tp->name, 
        length, 
        tp->seq,
        pp->protocol.dataSeqNum, 
        pp->protocol.flags & MLVPN_FLAGS_REORDER
    );
    pp->protocol.mlvpnSignature = htobe32(pp->protocol.mlvpnSignature);
    pp->protocol.dataSeqNum = htobe32(pp->protocol.dataSeqNum);
    pp->protocol.reorderSeqNum = htobe32(pp->protocol.reorderSeqNum);
    // Skip sequence number zero
    if (!tp->seq) {
        tp->seq = 1;
    }
    pp->protocol.tunnelSeqNum = htobe16(tp->seq++);
    pp->protocol.messageId = htobe16(pp->protocol.messageId);
    pp->protocol.dataLength = htobe16(pp->protocol.dataLength);
    
    //log_info("ARK", "%s mlvpn_rtun_send() len=%d", tp->name, (int)length);
    int ret;
    PROFILE_ENTER(PROFILE_RTUN_SEND_PKT_SENDTO);
    ret = sendto(
        tp->fd, 
        &pp->protocol, 
        length, 
        MSG_DONTWAIT, 
        tp->addrinfo->ai_addr, 
        tp->addrinfo->ai_addrlen
    );
    PROFILE_EXIT(PROFILE_RTUN_SEND_PKT_SENDTO);
    
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_warn("net", "%s write error", tp->name);
            mlvpn_rtun_status_down(tp);
        }
    } else {
        if (length != ret) {
            log_warnx("net", "%s write error %d/%u",
                tp->name, (int)ret, (unsigned int)length);
        }
        
        // Add to Tunnel send stats
        tp->stats.sendUpdated = TRUE;
        tp->stats.current.send.packets++;
        tp->stats.current.send.bytes += length;
    }
    
#if TUN_IO_WRITE
    if (ev_is_active(&tp->io_write) && !tp->sbuf.first && !tp->hsbuf.first) {
        ev_io_stop(EV_A_ &tp->io_write);
    }
#endif
    errCode = ERR_CODE_OK;
    
    if (pp) {
        MlvpnPacketsReturnFreePacket(pp);
    }
    PROFILE_EXIT(PROFILE_RTUN_SEND_PKT);
    return errCode;
}

/********************************************************************
 * Function: mlvpn_rtun_write
 *-------------------------------------------------------------------
 * Check for next packet(s) to send on tunnel
 *-------------------------------------------------------------------
 */
#if TUN_IO_WRITE
void mlvpn_rtun_write(EV_P_ ev_io *w, int revents) {
    mlvpn_tunnel_t* tp = w->data;
    
    while (tp->sbuf.first || tp->hsbuf.first) {
        mlvpn_rtun_send(tp);
    }
}
#endif

/********************************************************************
 * Function: mlvpn_queue_hsbuf
 *-------------------------------------------------------------------
 * Queue tunnel challenge message
 *-------------------------------------------------------------------
 */
void mlvpn_queue_hsbuf(mlvpn_tunnel_t *tp, mlvpn_pkt_t* pp) {
    mlvpn_queue_put(&tp->hsbuf, pp);
    pp->protocol.dataSeqNum = 0;
    pp->protocol.reorderSeqNum = 0;
    pp->protocol.flags = 0;
}

/********************************************************************
 * Function: mlvpn_rtun_challenge_send
 *-------------------------------------------------------------------
 * Queue tunnel challenge message
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_challenge_send(mlvpn_tunnel_t *tp) {
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        pp->data[0] = 'A';
        pp->data[1] = 'U';
        pp->protocol.dataLength = 2;
        pp->protocol.messageId = MLVPN_PKT_AUTH;
        mlvpn_queue_hsbuf(tp, pp);

        tp->status = MLVPN_AUTHSENT;
        log_info("protocol", "%s sending MLVPN_PKT_AUTH", tp->name);
    }
}

/********************************************************************
 * Function: mlvpn_rtun_send_auth
 *-------------------------------------------------------------------
 * Queue authentication messages
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_send_auth(mlvpn_tunnel_t* tp) {
    PROFILE_ENTER(PROFILE_RTUN_SEND_AUTH);
    
    if (gServerMode) {
        // Server side 
        if (tp->status == MLVPN_DISCONNECTED || tp->status >= MLVPN_AUTHOK) {
            mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
            if (pp) {
                pp->data[0] = 'O';
                pp->data[1] = 'K';
                pp->protocol.dataLength = 2;
                pp->protocol.messageId = MLVPN_PKT_AUTH_OK;
                mlvpn_queue_hsbuf(tp, pp);

                if (tp->status < MLVPN_AUTHOK) {
                    tp->status = MLVPN_AUTHSENT;
                }
                log_info("protocol", "%s sending MLVPN_PKT_AUTH_OK", tp->name);
                log_info("protocol", "%s authenticated", tp->name);
                mlvpn_rtun_status_up(tp);
                
                SpeedTestReset(tp);
            }
        }
    } else {
        // Client side
        if (tp->status == MLVPN_AUTHSENT) {
            log_info("protocol", "%s authenticated", tp->name);
            mlvpn_rtun_status_up(tp);
        }
    }
    PROFILE_EXIT(PROFILE_RTUN_SEND_AUTH);
}

/********************************************************************
 * Function: mlvpn_rtun_send_keepalive
 *-------------------------------------------------------------------
 * Queue keep alive message
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_send_keepalive(mlvpn_tunnel_t* tp) {
    PROFILE_ENTER(PROFILE_RTUN_SEND_KEEPALIVE);
    
    // Fill in data
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        mlvpn_pkt_keepalive_t* mp = (mlvpn_pkt_keepalive_t*)pp->data;
        memset(mp, 0, sizeof(*mp));
        mp->rttTimestampMsec = htobe64(mlvpn_timestamp_msec());
        
        if (gServerMode) {
            mp->speedTest.downloadTestNumber = htobe16(tp->speedTest.downloadTestNumberReport);
            
            if (tp->speedTest.runningUpload) {
                mp->speedTest.uploadTestNumber = htobe16(tp->speedTest.uploadTestNumber);
                mp->speedTest.uploadBatch = htobe16(tp->speedTest.batch);
                mp->speedTest.uploadMax = htobe32(tp->speedTest.max);
                mp->speedTest.uploadLength = htobe16(tp->speedTest.length);
                log_debug("protocol", "%s Tx KeepAlive uploadTestNumber:#%d downloadTestNumber:#%d", tp->name, tp->speedTest.uploadTestNumber, tp->speedTest.downloadTestNumberReport);
            } else {
                log_debug("protocol", "%s Tx KeepAlive downloadTestNumber:#%d", tp->name, tp->speedTest.downloadTestNumberReport);
            }
        } else {
            mp->speedTest.uploadTestNumber = htobe16(tp->speedTest.uploadTestNumberReport);
            log_debug("protocol", "%s Tx KeepAlive uploadTestNumber:#%d", tp->name, tp->speedTest.uploadTestNumberReport);
        }
        
        // Format packet and send
        pp->protocol.messageId = MLVPN_PKT_KEEPALIVE;
        pp->protocol.dataLength = sizeof(*mp);
        mlvpn_queue_hsbuf(tp, pp);
    }
    
    PROFILE_EXIT(PROFILE_RTUN_SEND_KEEPALIVE);
}

/********************************************************************
 * Function: mlvpn_rtun_send_keepalive_response
 *-------------------------------------------------------------------
 * Queue keep alive ack message
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_send_keepalive_response(
    mlvpn_tunnel_t* tp, 
    uint64_t        rttTimestampMsec
) {
    PROFILE_ENTER(PROFILE_RTUN_SEND_KEEPALIVE_RESPONSE);
    
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        mlvpn_pkt_keepalive_response_t* mp = (mlvpn_pkt_keepalive_response_t*)pp->data;
        memset(mp, 0, sizeof(*mp));
        mp->rttTimestampMsec = htobe64(rttTimestampMsec);

        if (tp->speedTest.downloadTestNumber == tp->speedTest.rxTestNumber) {
            mp->speedTest.downloadTestNumber = htobe16(tp->speedTest.rxTestNumber);
            mp->speedTest.downloadNumberPackets = htobe32(tp->speedTest.rxNumberPackets);
            mp->speedTest.downloadNumberBytes = htobe32(tp->speedTest.rxNumberBytes);
            uint16_t deltaTime = (uint16_t)(tp->speedTest.rxLastTime - tp->speedTest.rxStartTime);
            mp->speedTest.downloadDeltaTime = htobe16(deltaTime); 
        }
        mp->speedTest.uploadTestNumber = htobe16(tp->speedTest.uploadTestNumberReport);
        
        // Format packet and send
        pp->protocol.messageId = MLVPN_PKT_KEEPALIVE_RESPONSE;
        pp->protocol.dataLength = sizeof(*mp);
        log_debug("protocol", "%s Tx KeepAlive_Response", tp->name);
        mlvpn_queue_hsbuf(tp, pp);
    }
    
    PROFILE_EXIT(PROFILE_RTUN_SEND_KEEPALIVE_RESPONSE);
}

/********************************************************************
 * Function: mlvpn_rtun_send_client_stats
 *-------------------------------------------------------------------
 * Queue keep alive ack message
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_send_client_stats(mlvpn_tunnel_t* tp) {
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        mlvpn_pkt_client_stats_t* mp = (mlvpn_pkt_client_stats_t*)pp->data;
        memset(mp, 0, sizeof(*mp));

        mp->dataTimestampMsec = htobe64(mlvpn_timestamp_msec());            
        mp->sendMaxRateKbps = htobe32(tp->bandwidth);
        mp->tapReceiveQueue = htobe16(mlvpn_queue_count(&tuntap.rbuf));
        
        mp->receivedPackets = htobe32(tp->stats.totals.receive.packets);
        mp->receivedPacketsSec = htobe32(tp->stats.lastSec.receive.packets);
        
        mp->receivedBytes = htobe32(tp->stats.totals.receive.bytes);
        mp->receivedBytesSec = htobe32(tp->stats.lastSec.receive.bytes);
        
        mp->receivedOutOfOrder = htobe32(tp->stats.totals.receive.outOfOrder);
        mp->receivedOutOfOrderSec = htobe32(tp->stats.lastSec.receive.outOfOrder);
        
        mp->receivedOld = htobe32(tp->stats.totals.receive.old);
        mp->receivedOldSec = htobe32(tp->stats.lastSec.receive.old);
        
        mp->receivedLost = htobe32(tp->stats.totals.receive.lost);
        mp->receivedLostSec = htobe32(tp->stats.lastSec.receive.lost);
        
        // Format packet and send
        pp->protocol.messageId = MLVPN_PKT_CLIENT_STATS;
        pp->protocol.dataLength = sizeof(*mp);
        log_debug("protocol", "%s Tx Client_Stats", tp->name);
        mlvpn_queue_hsbuf(tp, pp);
    }
}

/********************************************************************
 * Function: mlvpn_rtun_send_disconnect
 *-------------------------------------------------------------------
 * Queue disconnect message
 *-------------------------------------------------------------------
 */
void mlvpn_rtun_send_disconnect(mlvpn_tunnel_t* tp) {
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        // Format packet and send
        pp->protocol.messageId = MLVPN_PKT_DISCONNECT;
        pp->protocol.dataLength = 0;
        log_debug("protocol", "%s sending disconnect", tp->name);
        mlvpn_queue_hsbuf(tp, pp);
    }
}

/********************************************************************
 * Function: mlvpn_control_pkt_send
 *-------------------------------------------------------------------
 * Send control packet.
 *-------------------------------------------------------------------
 */
void mlvpn_control_pkt_send(
    int32_t tun1_upload_kbps, 
    int32_t tun2_upload_kbps, 
    uint16_t reorder_timeout_ms, 
    uint16_t stats_timeout_ms
) {
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        // Format packet and send
        mlvpn_pkt_control_t* mp = (mlvpn_pkt_control_t*)pp->data;
        
        mp->tun1_upload_kbps = htobe32(tun1_upload_kbps);
        mp->tun2_upload_kbps = htobe32(tun2_upload_kbps);
        mp->reorder_timeout_ms = htobe16(reorder_timeout_ms);
        mp->stats_timeout_ms = htobe16(stats_timeout_ms);
        pp->protocol.messageId = MLVPN_PKT_CONTROL;
        pp->protocol.dataLength = sizeof(*mp);

        mlvpn_tunnel_t* tp = mlvpn_rtun_choose();
        if (!tp) {
            log_warnx("ARK", "Could not get tunnel");
            MlvpnPacketsReturnFreePacket(pp);
            return;
        }
        mlvpn_queue_hsbuf(tp, pp);
        log_debug("ARK", "%s mlvpn_control_send", tp->name);
    }
}

/********************************************************************
 * Function: mlvpn_status_tap_pkt_send
 *-------------------------------------------------------------------
 * Send status tap packet.
 *-------------------------------------------------------------------
 */
void mlvpn_status_tap_pkt_send(
    uint32_t    runTimeSec,
    
    uint32_t    sentPackets,
    uint32_t    sentBytes,
    uint32_t    sentPacketsSec,
    uint32_t    sentBytesSec,
    
    uint32_t    receivedPackets,
    uint32_t    receivedBytes,
    uint32_t    receivedPacketsSec,
    uint32_t    receivedBytesSec,
    
    uint32_t    receivedDiscard,
    uint32_t    receivedDiscardSec,
        
    uint32_t    sentSkipped,
    uint32_t    sentOutOfOrder,
    uint32_t    sentSkippedSec,
    uint32_t    sentOutOfOrderSec
) {
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        // Format packet and send
        mlvpn_pkt_status_tap_t* mp = (mlvpn_pkt_status_tap_t*)pp->data;
        
        mp->runTimeSec = htobe32(runTimeSec);
        mp->sentPackets = htobe32(sentPackets);
        mp->sentBytes = htobe32(sentBytes);
        mp->sentBytesSec = htobe32(sentBytesSec);
        
        mp->receivedPackets = htobe32(receivedPackets);
        mp->receivedBytes = htobe32(receivedBytes);
        mp->receivedPacketsSec = htobe32(receivedPacketsSec);
        mp->receivedBytesSec = htobe32(receivedBytesSec);
        
        mp->receivedDiscard = htobe32(receivedDiscard);
        mp->receivedDiscardSec = htobe32(receivedDiscardSec);
        
        mp->sentSkipped = htobe32(sentSkipped);
        mp->sentOutOfOrder = htobe32(sentOutOfOrder);
        mp->sentSkippedSec = htobe32(sentSkippedSec);
        mp->sentOutOfOrderSec = htobe32(sentOutOfOrderSec);
        
        pp->protocol.messageId = MLVPN_PKT_STATUS_TAP;
        pp->protocol.dataLength = sizeof(*mp);

        mlvpn_tunnel_t* tp = mlvpn_rtun_choose();
        if (!tp) {
            log_warnx("ARK", "Could not get tunnel");
            MlvpnPacketsReturnFreePacket(pp);
            return;
        }
        mlvpn_queue_hsbuf(tp, pp);
        log_debug("ARK", "%s mlvpn_status_tap_pkt_send", tp->name);
    }
}

/********************************************************************
 * Function: mlvpn_status_tunnel_pkt_send
 *-------------------------------------------------------------------
 * Send status tunnel packet.
 *-------------------------------------------------------------------
 */
void mlvpn_status_tunnel_pkt_send(
    uint16_t    tunnelIndex,
    uint32_t    runTimeSec,
    uint32_t    sendMaxRateKbps,
    
    uint32_t    sentPackets,
    uint32_t    sentBytes,
    uint32_t    sentPacketsSec,
    uint32_t    sentBytesSec,
    
    uint32_t    receivedPackets,
    uint32_t    receivedBytes,
    uint32_t    receivedPacketsSec,
    uint32_t    receivedBytesSec,
    
    uint32_t    receivedOutOfOrder,
    uint32_t    receivedOld,
    uint32_t    receivedOutOfOrderSec,
    uint32_t    receivedOldSec
) {
    mlvpn_pkt_t* pp = MlvpnPacketsGetNextFree();
    if (pp) {
        // Format packet and send
        mlvpn_pkt_status_tunnel_t* mp = (mlvpn_pkt_status_tunnel_t*)pp->data;
        
        mp->tunnelIndex = htobe16(tunnelIndex);
        mp->runTimeSec = htobe32(runTimeSec);
        mp->sendMaxRateKbps = htobe32(sendMaxRateKbps);
        
        mp->sentPackets = htobe32(sentPackets);
        mp->sentBytes = htobe32(sentBytes);
        mp->sentBytesSec = htobe32(sentBytesSec);
        
        mp->receivedPackets = htobe32(receivedPackets);
        mp->receivedBytes = htobe32(receivedBytes);
        mp->receivedPacketsSec = htobe32(receivedPacketsSec);
        mp->receivedBytesSec = htobe32(receivedBytesSec);
        
        mp->receivedOutOfOrder = htobe32(receivedOutOfOrder);
        mp->receivedOld = htobe32(receivedOld);
        mp->receivedOutOfOrderSec = htobe32(receivedOutOfOrderSec);
        mp->receivedOldSec = htobe32(receivedOldSec);
        
        pp->protocol.messageId = MLVPN_PKT_STATUS_TUNNEL;
        pp->protocol.dataLength = sizeof(*mp);

        mlvpn_tunnel_t* tp = mlvpn_rtun_choose();
        if (!tp) {
            log_warnx("ARK", "Could not get tunnel");
            MlvpnPacketsReturnFreePacket(pp);
            return;
        }
        mlvpn_queue_hsbuf(tp, pp);
        log_debug("ARK", "%s mlvpn_status_tunnel_pkt_send", tp->name);
    }
}
