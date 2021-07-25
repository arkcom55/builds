#include "includes.h"

#include <err.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netdb.h>
#include <linux/if_tun.h>
#include <linux/if.h>

#include "buffer.h"
#include "tuntap_generic.h"
#include "tool.h"

/********************************************************************
 * Function: mlvpn_tuntap_read
 *-------------------------------------------------------------------
 * Read from tap device
 *-------------------------------------------------------------------
 */
int mlvpn_tuntap_read(mlvpn_tuntap_t* tuntap) {
    PROFILE_ENTER(PROFILE_TUNTAP_READ);
    
    ssize_t result = 0;
    mlvpn_pkt_t* pp = NULL;
    
    // Check queue size 
    if (mlvpn_queue_count(&tuntap->rbuf) > MLVPN_MAX_RTUN_QUEUE) {
        tuntap->stats.current.receive.lost++;
        
        // Dummy read to avoid infinite loop
        char data[MLVPN_MTU];
        PROFILE_ENTER(PROFILE_TUNTAP_READ_READ);
        read(tuntap->fd, data, MLVPN_MTU);
        PROFILE_EXIT(PROFILE_TUNTAP_READ_READ);
        goto errorExit;
    }
    
    // Get packet to receive
    pp = MlvpnPacketsGetNextFree();
    if (!pp) {
        // Dummy read to avoid infinite loop
        char data[MLVPN_MTU];
        read(tuntap->fd, data, MLVPN_MTU);
        goto errorExit;
    }

    PROFILE_ENTER(PROFILE_TUNTAP_READ_READ);
    ssize_t ret = read(tuntap->fd, &pp->data, MLVPN_MTU);
    PROFILE_EXIT(PROFILE_TUNTAP_READ_READ);
    
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // read error on tuntap is not recoverable. We must die.
            fatal("tuntap", "unrecoverable read error");
        } else {
            // false reading from libev read would block, we can't read
            goto errorExit;
        }
    } else if (ret == 0) { // End of file
        fatalx("tuntap device closed");
        goto errorExit;
    } else if (ret > tuntap->maxmtu)  {
        log_warnx("tuntap",
            "cannot send packet: too big %d/%d. truncating",
            (uint32_t)ret, tuntap->maxmtu);
        goto errorExit;
    }
    pp->protocol.dataLength = ret;
    if (mlvpn_tuntap_generic_read(tuntap, pp)) {
        goto errorExit;
    }
    pp = NULL;
    result = ret;
    
  errorExit:
    if (pp) {
        MlvpnPacketsReturnFreePacket(pp);
    }
    
    PROFILE_EXIT(PROFILE_TUNTAP_READ);
    return result;
}

/********************************************************************
 * Function: mlvpn_tuntap_write
 *-------------------------------------------------------------------
 * Write to tap device
 *-------------------------------------------------------------------
 */
int mlvpn_tuntap_write(mlvpn_tuntap_t* tuntap) {
    PROFILE_ENTER(PROFILE_TUNTAP_WRITE);
    
    ssize_t ret = 0;

    // Get packet to send
    mlvpn_pkt_t* pp = mlvpn_queue_get(&tuntap->sbuf);
    if (!pp) {
        log_warn(
            "tuntap", 
            "%s mlvpn_tuntap_write no data to write", 
            tuntap->devname
        );
        goto errorExit;
    }
    
    // Write packet
    PROFILE_ENTER(PROFILE_TUNTAP_WRITE_WRITE);
    ret = write(tuntap->fd, pp->data, pp->protocol.dataLength);
    PROFILE_EXIT(PROFILE_TUNTAP_WRITE_WRITE);
    
    if (ret < 0) {
        log_warn("tuntap", "%s write error", tuntap->devname);
    } else {
        if (ret != pp->protocol.dataLength)
        {
            log_warnx(
                "tuntap", 
                "%s write error: %d/%d bytes sent",
                tuntap->devname, 
                (int)ret, 
                pp->protocol.dataLength
            );
        } else {
            // Count send TAP states
            tuntap->stats.sendUpdated = TRUE;
            tuntap->stats.current.send.packets++;
            tuntap->stats.current.send.bytes += ret;
            
            if (pp->protocol.dataSeqNum) {
                char* status = "";
                if (!tuntap->expectedSeqNum) {
                    tuntap->expectedSeqNum = 1;
                }

                uint32_t seq = pp->protocol.dataSeqNum;
                uint32_t expected = tuntap->expectedSeqNum;
                if (seq) {
                    uint32_t diff = seq - expected;
                    if (seq != expected) {
                        if (diff <= 0x7FFFFFFF) {
                            status = "out-of-order";
                            tuntap->stats.current.send.outOfOrder++;
                            tuntap->expectedSeqNum = seq + 1;
                        } else {
                            status = "old";
                            tuntap->stats.current.send.old++;
                        }
                    } else {
                        tuntap->expectedSeqNum = seq + 1;
                    }
                    
                    // See if new sequence number for lost detection
                    if (!tuntap->sendFirstSeqNum) {
                        tuntap->sendFirstSeqNum = seq;
                    } else {
                        diff = seq - tuntap->sendFirstSeqNum;
                        if (diff > 0x7FFFFFFF) {
                            tuntap->sendFirstSeqNum = seq;
                        }
                    }
                    if (!tuntap->sendLastSeqNum) {
                        tuntap->sendLastSeqNum = seq;
                    } else {
                        diff = seq - tuntap->sendLastSeqNum;
                        if (diff <= 0x7FFFFFFF) {
                            tuntap->sendLastSeqNum = seq;
                        }
                    }
                } else {
                    tuntap->expectedSeqNum = 1;
                }
                
                log_debug("TAP", "[SEND STAP %5d] %9d/%9d %s", pp->protocol.dataLength, seq, expected, status);
            } else {
                tuntap->expectedSeqNum = 1;
            }
            
            if (gServerMode) {
                // Add to channel stats
                mlvpnChannel_t* cp = &channels[pp->protocol.channel];
                if (!cp->initTime) {
                    cp->initTime = time(NULL);
                }
                cp->current.send.packets++;
                cp->current.send.bytes += ret;
                if (pp->protocol.flags & MLVPN_FLAGS_TCP) {
                    cp->current.send.tcpPackets++;
                    cp->current.send.tcpBytes += ret;
                } else if (pp->protocol.flags & MLVPN_FLAGS_UDP) {
                    cp->current.send.udpPackets++;
                    cp->current.send.udpBytes += ret;
                }
                cp->sendUpdated = TRUE;
            }
        }
    }
    MlvpnPacketsReturnFreePacket(pp);
    
  errorExit:
    PROFILE_EXIT(PROFILE_TUNTAP_WRITE);
    return ret;
}

/********************************************************************
 * Function: root_tuntap_open
 *-------------------------------------------------------------------
 * Open tap device
 *-------------------------------------------------------------------
 */
int root_tuntap_open(int tuntapmode, char *devname, int mtu) {
    struct ifreq ifr;
    int fd, sockfd;

    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        warn("failed to open /dev/net/tun");
    } else {
        memset(&ifr, 0, sizeof(ifr));
        if (tuntapmode == MLVPN_TUNTAPMODE_TAP) {
            ifr.ifr_flags = IFF_TAP;
        } else {
            ifr.ifr_flags = IFF_TUN;
        }
        
        /* We do not want kernel packet info (IFF_NO_PI) */
        ifr.ifr_flags |= IFF_NO_PI;

        /* Allocate with specified name, otherwise the kernel
         * will find a name for us. */
        if (*devname) {
            mystrlcpy(ifr.ifr_name, devname, sizeof(ifr.ifr_name));
        }
        
        /* ioctl to create the if */
        if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
            /* don't call fatal because we want a clean nice error for the
             * unprivilged process.
             */
            warn("open tun %s ioctl failed", devname);
            close(fd);
            return -1;
        }

        /* set tun MTU */
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            warn("socket creation failed");
        } else {
            ifr.ifr_mtu = mtu;
            if (ioctl(sockfd, SIOCSIFMTU, &ifr) < 0) {
                warn("unable to set tun %s mtu=%d", devname, mtu);
                close(fd);
                close(sockfd);
                return -1;
            }
            close(sockfd);
        }
    }
    /* The kernel is the only one able to "name" the if.
     * so we reread it to get the real name set by the kernel. */
    if (fd > 0) {
        mystrlcpy(devname, ifr.ifr_name, MLVPN_IFNAMSIZ);
    }
    return fd;
}

/********************************************************************
 * Function: mlvpn_tuntap_alloc
 *-------------------------------------------------------------------
 * Allocate tap device
 *-------------------------------------------------------------------
 */
int mlvpn_tuntap_alloc(mlvpn_tuntap_t* tuntap) {
    int fd;

    fd = root_tuntap_open(
        tuntap->type,        
        tuntap->devname, 
        tuntap->maxmtu
    );

    if (fd <= 0) {
        fatalx("failed to open /dev/net/tun read/write");
    }
    tuntap->fd = fd;
    
    return fd;
}


