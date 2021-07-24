#ifndef _INCLUDES_H
#define _INCLUDES_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <pwd.h>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include "config.h"
#include "defines.h"

#include <inttypes.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <math.h>
#include <ev.h>

/*
 * We want functions in openbsd-compat, if enabled, to override system ones.
 * We no-op out the weak symbol definition rather than remove it to reduce
 * future sync problems.
 */
#define DEF_WEAK(x)

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_CLOSEFROM
void closefrom(int lowfd);
#endif

/* Many thanks Fabien Dupont! */
#ifdef HAVE_LINUX
 /* Absolutely essential to have it there for IFNAMSIZ */
 #include <sys/types.h>
 #include <netdb.h>
 #include <linux/if.h>
#endif

#include <arpa/inet.h>

#ifdef HAVE_VALGRIND_VALGRIND_H
 #include <valgrind/valgrind.h>
#else
 #define RUNNING_ON_VALGRIND 0
#endif

#ifdef HAVE_DECL_RES_INIT
 #include <netinet/in.h>
 #include <arpa/nameser.h>
 #include <resolv.h>
#endif

#include "pkt.h"
#include "buffer.h"
#include "reorder.h"
#include "timestamp.h"
#include "mlvpn.h"
#include "mlvpn_send.h"
#include "mlvpn_recv.h"
#include "wrr.h"
#include "speedtest.h"
#include "configlib.h"

#include "privsep.h"
#include "log.h"

#include "tool.h"
#include "setproctitle.h"
#include "crypto.h"
#ifdef ENABLE_CONTROL
#include "control.h"
#endif
#include "tuntap_generic.h"

/* Linux specific things */
#ifdef HAVE_LINUX
#include <sys/prctl.h>
#include "systemd.h"
#endif

#ifdef HAVE_FREEBSD
#define _NSIG _SIG_MAXSIG
#include <sys/endian.h>
#endif

#ifdef HAVE_DARWIN
#include <libkern/OSByteOrder.h>
#define be16toh OSSwapBigToHostInt16
#define be32toh OSSwapBigToHostInt32
#define be64toh OSSwapBigToHostInt64
#define htobe16 OSSwapHostToBigInt16
#define htobe32 OSSwapHostToBigInt32
#define htobe64 OSSwapHostToBigInt64
#endif

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

size_t mystrlcpy(char *dst, const char *src, size_t size);
size_t mystrlcat(char *dst, const char *src, size_t size);

#define ERR_CODE_OK     (0)
#define ERR_CODE_ERROR  (1)     // Generic error

int MlvpnPacketsAllocate();
mlvpn_pkt_t* MlvpnPacketsGetNextFree();
int MlvpnPacketsReturnFreePacket(mlvpn_pkt_t* pp);

int mlvpn_config(int config_file_fd);

#endif
