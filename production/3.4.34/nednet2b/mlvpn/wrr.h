#ifndef _WRR_H
#define _WRR_H

int mlvpnBWSet(int* kbps);
int mlvpn100ms(void);
int mlvpn10ms(void);
int mlvpnBwStatus(char* bp);

void monitor_error_rate(
    mlvpn_tunnel_t*     tp,
    int                 download,
    uint64_t            dataTimestampMsec, 
    uint32_t            packets,
    uint32_t            bytes,
    uint32_t            lost
);

int mlvpn_clear_tunnel_stats(mlvpn_tunnel_t* tp);

#define PROFILE (1)

#if PROFILE

typedef struct {
    uint16_t    index;
    char        name[80];
    uint32_t    entryCount;
    uint32_t    exitCount;
    uint32_t    recursiveCount;
    uint64_t    enterTimeUsec;
    uint64_t    totalTimeUsec;
    uint64_t    maxTimeUsec;
} Profile_t, *Profile_tp;

#define PROFILE_MAIN                            (0)

#define PROFILE_TUNTAP_READ                     (1)
#define PROFILE_TUNTAP_READ_READ                (2)

#define PROFILE_IDLE                            (3)
#define PROFILE_RTUN_SEND_PKT                   (4)
#define PROFILE_RTUN_SEND_PKT_SENDTO            (5)

#define PROFILE_RTUN_READ                       (6)
#define PROFILE_RTUN_READ_RECVFROM              (7)
#define PROFILE_PROTOCOL_READ                   (8)
#define PROFILE_RTUN_RECV_DATA                  (9)
#define PROFILE_REORDER                         (10)
#define PROFILE_INJECT_TUNTAP                   (11)
#define PROFILE_RTUN_SEND_AUTH                  (12)
#define PROFILE_RTUN_STATUS_UP                  (13)
#define PROFILE_RTUN_SEND_KEEPALIVE_RESPONSE    (14)

#define PROFILE_TUNTAP_WRITE                    (15)
#define PROFILE_TUNTAP_WRITE_WRITE              (16)

#define PROFILE_RTUN_STATUS_DOWN                (17)
#define PROFILE_RTUN_SEND_KEEPALIVE             (18)
#define PROFILE_SPEEDTEST_RESET                 (19)

#define PROFILE_100_MS                          (20)
#define PROFILE_10_MS                           (21)

#define PROFILE_LOG_INFO                        (22)
#define PROFILE_LOG_DEBUG                       (23)

#define PROFILE_COUNT                           (24)

extern Profile_t gProfile[PROFILE_COUNT];

void ProfileEnter(uint16_t index);
void ProfileExit(uint16_t index);
void ProfileDump(char* tag);

#define PROFILE_ENTER(index)    ProfileEnter(index)
#define PROFILE_EXIT(index)     ProfileExit(index)

#else

#define PROFILE_ENTER(index)
#define PROFILE_EXIT(index)

#endif

#endif
