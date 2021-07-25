#ifndef _MLVPN_RECV_H
#define _MLVPN_RECV_H

void mlvpn_rtun_read(EV_P_ ev_io *w, int revents);
int mlvpn_protocol_read(
    mlvpn_tunnel_t *tun,        // Pointer to tunnel
    mlvpn_pkt_t *pp             // Pointer to incoming packet
);
int mlvpn_rtun_recv_data(mlvpn_tunnel_t* tun, mlvpn_pkt_t* pp);

/*
void mlvpn_rtun_reorder_drain_timeout(EV_P_ ev_timer *w, int revents);
void mlvpn_rtun_reorder_drain_timeout_stop();
void mlvpn_rtun_reorder_drain_timeout_start();
void mlvpn_rtun_reorder_drain_timeout_restart();
*/

#endif
