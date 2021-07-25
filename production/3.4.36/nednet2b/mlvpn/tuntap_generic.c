#include "includes.h"

/********************************************************************
 * Function: mlvpn_tuntap_generic_read
 *-------------------------------------------------------------------
 * Post read from tap device
 *-------------------------------------------------------------------
 */
int mlvpn_tuntap_generic_read(mlvpn_tuntap_t* tuntap, mlvpn_pkt_t* pp) {
    int errCode = ERR_CODE_ERROR;
    
    if (!pp) {
        goto errorExit;
    }

    if (pp->protocol.dataLength < 20) {
        log_info("RTAP", "Too few bytes = %d", pp->protocol.dataLength);
        goto errorExit;
    } else {
        // Check version
        char* data = pp->data;
        if ((data[0] & 0xF0) != 0x40) {
            log_debug("RTAP", "Tap: Wrong version # = %d", data[0] >> 4);
            goto errorExit;
        } else {
            int protocol = data[9];
            
            if (protocol == 6) {
                // TCP packet
                pp->protocol.flags = MLVPN_FLAGS_TCP;
            } else if (protocol == 17) {
                // UDP packet
                pp->protocol.flags = MLVPN_FLAGS_UDP;
            } else {
                 pp->protocol.flags = 0;
            }
            
            // Analyze packet to get destination IP address
            uint16_t    dest4;
            
            if (gServerMode) {
                dest4 = data[19] & 0x00FF;
            } else {
                dest4 = data[15] & 0x00FF;
            }

            // Determine "channel" id
            uint16_t channel = dest4 >> 2;
            if (channel >= MLVPN_NUM_CHANNELS) {
                log_info("ARK", "mlvpn_tuntap_generic_read invalid channel=%d", channel);
                goto errorExit;
            } else {
                pp->protocol.channel = channel;
            }
            
            /*
            dscp = data[1] >> 2;
            int tlength = (data[2] << 8) + data[3];
            
            int ihl = data[0] & 0x0F;
            int ecn = data[1] & 0x03;
            int tlength = (data[2] << 8) + data[3];
            //log_debug("RTAP", "ihl=%d dscp=%d ecn=%d tlength=%d", ihl, dscp, ecn, tlength);
            int id = (data[4] << 8) + data[5];
            int flags = data[6] >> 5;
            int fragOffset = ((data[6] & 0x1F) << 8) + data[7];
            int ttl = data[8];
            int headerCheckum = (data[10] << 8) + data[11];
            log_debug("RTAP", "id=%d flags=%d frag=%d ttl=%d", id, flags, fragOffset, ttl);
            log_debug("RTAP", "protocol=%d src=%d.%d.%d.%d dst=%d.%d.%d.%d", protocol, data[12], data[13], data[14], data[15], data[16], data[17], data[18], data[19]);
            
            if (gServerMode) {
                dest[0] = data[16];
                dest[1] = data[17];
                dest[2] = data[18];
                dest[3] = data[19];
            } else {
                dest[0] = data[12];
                dest[1] = data[13];
                dest[2] = data[14];
                dest[3] = data[15];
            }
            if (protocol == 6) {
                // TCP packet
                u_char* dp = &data[20];
                int sport = (dp[0] << 8) | dp[1];
                int dport = (dp[2] << 8) | dp[3];
                uint32_t seq = (dp[4] << 24) | (dp[5] << 16) | (dp[6] << 8) | dp[7];
                uint32_t ack = (dp[8] << 24) | (dp[9] << 16) | (dp[10] << 8) | dp[11];
                int dataOffset = dp[12] >> 4;
                int flags = ((dp[12] & 0x01) << 8) | dp[13];
                int win = (dp[14] << 8) | dp[15];
                //int checksum = (dp[16] << 8) | dp[17];
                //int urgent = (dp[18] << 8) | dp[19];
                log_debug("RTAP", "TCP: %d/%d seq=%u ack=%u hsize=%d flags=0x%X win=%d len=%d", sport, dport, seq, ack, dataOffset * 4, flags, win, tlength);
            }
            */
            
            pp->protocol.messageId = MLVPN_PKT_DATA;
            if (pp->protocol.flags & MLVPN_FLAGS_UDP) {
                pp->protocol.reorderSeqNum = tuntap->udpReorderSeqNum++;
            } else if (pp->protocol.flags & MLVPN_FLAGS_TCP) {
                pp->protocol.reorderSeqNum = tuntap->tcpReorderSeqNum++;
            }
            pp->protocol.dataSeqNum = tuntap->dataSeqNum++;

            // Add to queue
            mlvpn_queue_put(&tuntap->rbuf, pp);
           
            log_debug(
                "TAP", 
                "[RECV RTAP %5d] %9d R:%d", 
                pp->protocol.dataLength, 
                pp->protocol.dataSeqNum,
                pp->protocol.flags & MLVPN_FLAGS_REORDER
            );
            
            // Add to TAP receive stats
            tuntap->stats.current.receive.packets++;
            tuntap->stats.current.receive.bytes += pp->protocol.dataLength;
            tuntap->stats.receiveUpdated = TRUE;
            
            if (gServerMode) {
                // Add to channel stats
                mlvpnChannel_t* cp = &channels[channel];
                if (!cp->initTime) {
                    cp->initTime = time(NULL);
                }
                cp->current.receive.packets++;
                cp->current.receive.bytes += pp->protocol.dataLength;
                if (pp->protocol.flags & MLVPN_FLAGS_TCP) {
                    cp->current.receive.tcpPackets++;
                    cp->current.receive.tcpBytes += pp->protocol.dataLength;
                } else if (pp->protocol.flags & MLVPN_FLAGS_UDP) {
                    cp->current.receive.udpPackets++;
                    cp->current.receive.udpBytes += pp->protocol.dataLength;
                }
                cp->receiveUpdated = TRUE;
            }
        }
    }
    
    errCode = ERR_CODE_OK;
    
  errorExit:
    return errCode;
}
