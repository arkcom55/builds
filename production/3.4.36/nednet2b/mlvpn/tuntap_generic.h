#ifndef MLVPN_TUNTAP_GENERIC_H
#define MLVPN_TUNTAP_GENERIC_H

#if 0
#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <ev.h>

#include "buffer.h"
#include "privsep.h"
#include "mlvpn.h"
#endif

int mlvpn_tuntap_alloc(mlvpn_tuntap_t* tuntap);
int mlvpn_tuntap_read(mlvpn_tuntap_t* tuntap);
int mlvpn_tuntap_write(mlvpn_tuntap_t* tuntap);
int mlvpn_tuntap_generic_read(mlvpn_tuntap_t* tuntap, mlvpn_pkt_t* pp);

#endif
