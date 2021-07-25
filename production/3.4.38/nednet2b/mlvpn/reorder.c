/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *   Adapted for mlvpn by Laurent Coustet (c) 2015
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

typedef struct {
    uint32_t        seqNum;
    uint16_t        size;
    uint16_t        mask;
    uint16_t        start;
    uint16_t        count;
    mlvpn_pkt_t**   buffer;
    uint64_t        timestamp;
    uint16_t        timeout;
} ReorderBuffer_t, *ReorderBuffer_tp;

static ReorderBuffer_t sReorderBuffer;
static ReorderBuffer_t sTcpReorderBuffer;

/********************************************************************
 * mlvpn_reorder_init_internal
 *-------------------------------------------------------------------
 */
int mlvpn_reorder_init_internal(ReorderBuffer_tp rbp, int reorderBufferSize, uint16_t timeout) {

    memset(rbp, 0, sizeof(*rbp));
    
    // Make size power of two 
    if (reorderBufferSize < 2) {
        reorderBufferSize = 0;
    } else {
        reorderBufferSize = pow(2, ceil(log(reorderBufferSize) / log(2)));
        rbp->size = reorderBufferSize;
        rbp->mask = rbp->size - 1;
        rbp->buffer = calloc(rbp->size, sizeof(mlvpn_pkt_t*));
        rbp->timeout = timeout;
    }
    
    return reorderBufferSize;
}

/********************************************************************
 * mlvpn_reorder_init
 *-------------------------------------------------------------------
 */
int mlvpn_reorder_init(int reorderBufferSize, uint16_t timeout) {
    int errCode = ERR_CODE_ERROR;
    
    reorderBufferSize = mlvpn_reorder_init_internal(&sReorderBuffer, reorderBufferSize, timeout);
    log_info("REO", "mlvpn_reorder_init: size set to %d, timeout %d ms", reorderBufferSize, timeout);  
    errCode = ERR_CODE_OK;
    
    return errCode;
}

/********************************************************************
 * mlvpn_reorder_init_tcp
 *-------------------------------------------------------------------
 */
int mlvpn_reorder_init_tcp(int reorderBufferSize, uint16_t timeout) {
    int errCode = ERR_CODE_ERROR;
    
    reorderBufferSize = mlvpn_reorder_init_internal(&sTcpReorderBuffer, reorderBufferSize, timeout);
    log_info("REO", "mlvpn_reorder_init_tcp: size set to %d, timeout %d ms", reorderBufferSize, timeout);  
    errCode = ERR_CODE_OK;
    
    return errCode;
}

/********************************************************************
 * mlvpn_reorder_init
 *-------------------------------------------------------------------
 */
int mlvpn_reorder_reinit() {
    int errCode = ERR_CODE_ERROR;
    
    if (sReorderBuffer.size) {
        sReorderBuffer.count = 0;
        sReorderBuffer.start = 0;
        sReorderBuffer.seqNum = 0;
        sReorderBuffer.timestamp = 0;
        memset(sReorderBuffer.buffer, 0, sReorderBuffer.size * sizeof(mlvpn_pkt_t*));
    }
    
    if (sTcpReorderBuffer.size) {
        sTcpReorderBuffer.count = 0;
        sTcpReorderBuffer.start = 0;
        sTcpReorderBuffer.seqNum = 0;
        sReorderBuffer.timestamp = 0;
        memset(sTcpReorderBuffer.buffer, 0, sTcpReorderBuffer.size * sizeof(mlvpn_pkt_t*));
    }
    
    errCode = ERR_CODE_OK;
    
    return errCode;
}

/********************************************************************
 * mlvpn_reorder
 *-------------------------------------------------------------------
 */
int mlvpn_reorder(mlvpn_pkt_t* pp) {
    PROFILE_ENTER(PROFILE_REORDER);
    
    int errCode = ERR_CODE_ERROR;
    ReorderBuffer_tp rbp = NULL;
    
    // Check for packet
    if (!pp) {
        goto errorExit;
    }
    
    // Determine reorder buffer
    if (pp->protocol.flags & MLVPN_FLAGS_UDP) {
        rbp = &sReorderBuffer;
    } else if (pp->protocol.flags & MLVPN_FLAGS_TCP) {
        rbp = &sTcpReorderBuffer;
    }
    
    // Check if reorder buffer
    if (!rbp || !rbp->size) {
        // Write out packet immediately
        if (mlvpn_rtun_inject_tuntap(pp)) {
            goto errorExit;
        }
        goto okExit;
    }
    
    int firstPacket = FALSE;
    if (!rbp->count) {
        firstPacket = TRUE;
    }
    
    // Insert in reorder buffer
    uint32_t seq = pp->protocol.reorderSeqNum; 
    uint32_t offset = seq - rbp->seqNum;
    int numOut = 0;
    
    // Check if old sequence
    if (offset > 0x7FFFFFFF) {
        // Have to send out immediately
        log_debug("REO", "seq:%d too old, send immediately", seq);
        if (mlvpn_rtun_inject_tuntap(pp)) {
            MlvpnPacketsReturnFreePacket(pp);
            //numOut++;
        }
    } else {
        // Check if it fits in reorder buffer
        if (offset >= rbp->size) {
            // Does not fit, empty buffer until room
            int count = offset - rbp->size + 1;
            if (count > rbp->size) {
                count = rbp->size;
                rbp->seqNum = seq;
            } else {
                rbp->seqNum += count;
            }
            log_debug("REO", "seq:%d does not fit, advancing %d locations", seq, count);
            for (; count; count--) {
                if (rbp->buffer[rbp->start]) {
                    mlvpn_pkt_t* pp2 = rbp->buffer[rbp->start];
                    rbp->buffer[rbp->start] = NULL;
                    rbp->count--;
                    numOut++;
                    if (mlvpn_rtun_inject_tuntap(pp2)) {
                        MlvpnPacketsReturnFreePacket(pp2);
                    }
                }
                rbp->start = (rbp->start + 1) & rbp->mask;
            }
            offset = seq - rbp->seqNum;
        }
        
        // Insert in buffer
        int i = (rbp->start + offset) & rbp->mask;
        rbp->buffer[i] = pp;
        rbp->count++;
        
        // Write out all in order packets
        for (int count = 0; count < rbp->size; count++) {
            if (rbp->buffer[rbp->start]) {
                mlvpn_pkt_t* pp2 = rbp->buffer[rbp->start];
                rbp->buffer[rbp->start] = NULL;
                rbp->count--;
                numOut++;
                if (mlvpn_rtun_inject_tuntap(pp2)) {
                    MlvpnPacketsReturnFreePacket(pp2);
                }
            } else {
                break;
            }
            rbp->start = (rbp->start + 1) & rbp->mask;
            rbp->seqNum++;
        }
    }
    
    if (!rbp->count) {
        rbp->timestamp = 0;
    } else if (numOut) {
        rbp->timestamp = mlvpn_timestamp_msec();
    } else if (firstPacket) {
        rbp->timestamp = mlvpn_timestamp_msec();
    } else {
        //log_debug("REO", "No action, reorderBufferCount=%d numOut=%d firstPacket=%d", rbp->count, numOut, firstPacket);
    }

  okExit:
    errCode = ERR_CODE_OK;
    
  errorExit:
    PROFILE_EXIT(PROFILE_REORDER);
    return errCode;
}

/********************************************************************
 * mlvpn_reorder_flush_internal
 *-------------------------------------------------------------------
 */
int mlvpn_reorder_flush_internal(ReorderBuffer_tp rbp) {
    int num = 0;
    
    if (!rbp || !rbp->count || !rbp->timeout) {
        goto okExit;
    }

    if (rbp->timestamp) {
        uint16_t deltaTime = (uint16_t)(mlvpn_timestamp_msec() - rbp->timestamp);
        if (deltaTime < rbp->timeout) {
            goto okExit;
        }
    }
    
    int i = rbp->start;
    uint32_t lastSeq = 0;
    for (int count = 0; count < rbp->size; count++) {
        if (!rbp->count) {
            break;
        }
        if (rbp->buffer[i]) {
            mlvpn_pkt_t* pp = rbp->buffer[i];
            rbp->buffer[i] = NULL;
            num++;
            lastSeq = pp->protocol.reorderSeqNum; 
            if (mlvpn_rtun_inject_tuntap(pp)) {
                MlvpnPacketsReturnFreePacket(pp);
            }
            rbp->count--;
        }
        i = (i + 1) & rbp->mask;
    }
    rbp->start = 0;
    rbp->count = 0;
    rbp->timestamp = 0;
    rbp->seqNum = lastSeq + 1;
    
    log_info("REO", "Flushed %d packets", num);
        
  okExit:
    return ERR_CODE_OK;
}

/********************************************************************
 * mlvpn_reorder_flush
 *-------------------------------------------------------------------
 */
int mlvpn_reorder_flush() {
    
    // Check if need to flush either reorder buffer
    mlvpn_reorder_flush_internal(&sReorderBuffer);
    mlvpn_reorder_flush_internal(&sTcpReorderBuffer);
    
    return ERR_CODE_OK;
}

