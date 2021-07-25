#ifndef _MLVPN_SEND_H
#define _MLVPN_SEND_H

int mlvpn_rtun_inject_tuntap(mlvpn_pkt_t* pp);

int mlvpn_rtun_send(mlvpn_tunnel_t* tp);

int mlvpn_rtun_send_pkt(mlvpn_tunnel_t* tp, mlvpn_pkt_t* pp);

void mlvpn_rtun_write(EV_P_ ev_io *w, int revents);

void mlvpn_queue_hsbuf(mlvpn_tunnel_t *tp, mlvpn_pkt_t* pp);

void mlvpn_rtun_challenge_send(mlvpn_tunnel_t *tp);
void mlvpn_rtun_send_auth(mlvpn_tunnel_t* tp);

void mlvpn_rtun_send_keepalive(mlvpn_tunnel_t* tp);
void mlvpn_rtun_send_keepalive_response(
    mlvpn_tunnel_t* tp, 
    uint64_t        rttTimestampMsec
);
void mlvpn_rtun_send_client_stats(mlvpn_tunnel_t* tp);

void mlvpn_rtun_send_disconnect(mlvpn_tunnel_t* tp);

void mlvpn_control_pkt_send(
    int32_t tun1_upload_kbps, 
    int32_t tun2_upload_kbps, 
    uint16_t reorder_timeout_ms, 
    uint16_t stats_timeout_ms
);

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
);

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
);

#endif
