#ifndef _MLVPN_H
#define _MLVPN_H

#define TUN_IO_WRITE (0)

#define MLVPN_NUM_CHANNELS      (40)
#define MLVPN_MAX_RTUN_QUEUE    (500)
#define MAX_BANDWIDTH_KBPS      (30000)

#define MLVPN_MAXHNAMSTR 256
#define MLVPN_MAXPORTSTR 6

/* Number of packets in the queue. Each pkt is ~ 1520 */
/* 1520 * 128 ~= 24 KBytes of data maximum per channel VMSize */
#define PKTBUFSIZE 1024

/* tuntap interface name size */
#ifndef IFNAMSIZ
 #define IFNAMSIZ 16
#endif
#define MLVPN_IFNAMSIZ IFNAMSIZ

/* How frequently we check tunnels */
#define MLVPN_IO_TIMEOUT_DEFAULT 1.0
/* What is the maximum retry timeout */
#define MLVPN_IO_TIMEOUT_MAXIMUM 60.0
/* In case we can't open the tunnel, retry every time with previous
 * timeout multiplied by the increment.
 * Example:
 * 1st try t+0: bind error
 * 2nd try t+1: bind error
 * 3rd try t+2: bind error
 * 4rd try t+4: dns error
 * ...
 * n try t+60
 * n+1 try t+60
 */
#define MLVPN_IO_TIMEOUT_INCREMENT 2

#define NEXT_KEEPALIVE(now, t) (now + 1)
/* Protocol version of mlvpn
 * version 0: mlvpn 2.0 to 2.1 
 * version 1: mlvpn 2.2+ (add reorder field in mlvpn_proto_t)
 */
#define MLVPN_PROTOCOL_VERSION 1

/********************************************************************
 * Options
 *-------------------------------------------------------------------
 */
struct mlvpn_options_s
{
    /* use ps_status or not ? */
    int         change_process_title;
    /* process name if set */
    char        process_name[1024];
    
    /* where is the config file */
    char        control_unix_path[MAXPATHLEN];
    char        control_bind_host[MLVPN_MAXHNAMSTR];
    char        control_bind_port[MLVPN_MAXHNAMSTR];
    char        config_path[MAXPATHLEN];

    /* tunnel configuration for the status command script */
    char        ip4[16];
    char        ip6[128]; /* Should not exceed 45 + 3 + 1 bytes */
    char        ip4_gateway[16];
    char        ip6_gateway[128];
    int         mtu;
    int         config_fd;
    int         verbose;
    int         debug;
    int         timeout;
    int         initial_speedtest;
    int         autospeed;
    uint16_t    dslWeight;
    
    uint16_t    reorder_buffer_size;
    uint16_t    reorder_buffer_timeout_ms;
    uint16_t    tcp_reorder_buffer_size;
    uint16_t    tcp_reorder_buffer_timeout_ms;
    
    char        status_command[MAXPATHLEN];
};
extern struct mlvpn_options_s mlvpn_options;

struct mlvpn_status_s
{
    int fallback_mode;
    int connected;
    int initialized;
    time_t start_time;
    time_t last_reload;
};

extern int gServerMode;

/********************************************************************
 * Stats Structures
 *-------------------------------------------------------------------
 */
typedef struct {
    uint32_t    packets;
    uint64_t    bytes;
    uint32_t    outOfOrder;
    uint32_t    old;
    uint32_t    lost;
} mlvpnSendCounts_t;

typedef struct {
    uint32_t    packets;
    uint64_t    bytes;
    uint32_t    outOfOrder;
    uint32_t    old;
    uint32_t    lost;
} mlvpnReceiveCounts_t;

typedef struct {
    mlvpnSendCounts_t       send;
    mlvpnReceiveCounts_t    receive;
} mlvpnCounts_t;

typedef struct {
    uint32_t        totalTimeSec;
    mlvpnCounts_t   totals;
    mlvpnCounts_t   lastSec;    
    mlvpnCounts_t   current; 
    int             sendUpdated;
    int             receiveUpdated;
} mlvpnStats_t;

typedef struct {
    uint32_t    packets;
    uint64_t    bytes;
    uint32_t    tcpPackets;
    uint64_t    tcpBytes;
    uint32_t    udpPackets;
    uint64_t    udpBytes;    
} mlvpnChannelCounts_t;

typedef struct {
    mlvpnChannelCounts_t    send;
    mlvpnChannelCounts_t    receive;
} mlvpnChannelStats_t;

typedef struct {
    uint32_t                initTime;
    mlvpnChannelStats_t     totals;
    mlvpnChannelStats_t     lastSec;
    mlvpnChannelStats_t     current;
    int                     sendUpdated;
    int                     receiveUpdated;
} mlvpnChannel_t;

extern mlvpnChannel_t   channels[MLVPN_NUM_CHANNELS];

/********************************************************************
 * Packet Buffer
 *-------------------------------------------------------------------
 */

typedef struct {
    mlvpn_pkt_t*    first;
    mlvpn_pkt_t*    last;
    uint16_t        count;
} mlvpn_pkt_buffer_t;

/********************************************************************
 * Tap Structure
 *-------------------------------------------------------------------
 */
enum tuntap_type {
    MLVPN_TUNTAPMODE_TUN,
    MLVPN_TUNTAPMODE_TAP
};

typedef struct tuntap_s
{
    int                 fd;
    int                 maxmtu;
    char                devname[MLVPN_IFNAMSIZ];
    enum tuntap_type    type;
    mlvpn_pkt_buffer_t  sbuf;
    mlvpn_pkt_buffer_t  rbuf;
    uint32_t            dataSeqNum;
    
    uint32_t            udpReorderSeqNum;
    uint32_t            tcpReorderSeqNum;

    uint32_t            expectedSeqNum;
    ev_io               io_read;
    ev_io               io_write;
    mlvpnStats_t        stats;
    uint32_t            sendFirstSeqNum;
    uint32_t            sendLastSeqNum;
    uint32_t            initTime; 
} mlvpn_tuntap_t;      

extern mlvpn_tuntap_t      tuntap;

/********************************************************************
 * Tunnel Structure
 *-------------------------------------------------------------------
 */
enum chap_status {
    MLVPN_DISCONNECTED,
    MLVPN_AUTHSENT,
    MLVPN_AUTHOK,
    MLVPN_LOSSY
};

#define RX_ERRORS_BINS                      (4)

typedef struct {
    uint32_t    packets;
    uint32_t    bytes;
    uint32_t    errors;
} rx_errors_bin_t;

typedef struct {
    uint64_t    lastTime;
    uint64_t    lastTimeDown;
    uint16_t    last;
    uint16_t    count;
    rx_errors_bin_t bins[RX_ERRORS_BINS];
    uint32_t    totalPackets;
    uint32_t    totalBytes;
    uint32_t    totalErrors;
} rx_errors_t;

typedef struct mlvpn_tunnel_s
{
    struct mlvpn_tunnel_s*  next;
    struct mlvpn_tunnel_s*  previous;
    char*                   name;           /* tunnel name */
    enum chap_status        status;         /* Auth status */
    uint32_t                bandwidth;      /* bandwidth in bytes per second */
    uint32_t                ulBandwidth;    /* bandwidth in bytes per second */

    char                    bindaddr[MLVPN_MAXHNAMSTR]; /* packets source */
    char                    bindport[MLVPN_MAXPORTSTR]; /* packets port source (or NULL) */
    uint32_t                bindfib;                    /* FIB number to use */
    char                    destaddr[MLVPN_MAXHNAMSTR]; /* remote server ip (can be hostname) */
    char                    destport[MLVPN_MAXPORTSTR]; /* remote server port */
    struct                  addrinfo *addrinfo;

    int                     fd;                 /* socket file descriptor */
    int                     disconnects;        /* is it stable ? */
    int                     conn_attempts;      /* connection attempts */
    uint16_t                seq;
    uint16_t                expected_receiver_seq;
    uint64_t                seq_last;

    uint32_t                timeout;            /* configured timeout in seconds */
    uint32_t                timeoutRunning;     /* configured timeout in seconds */

    //ev_tstamp last_activity;
    ev_tstamp               last_connection_attempt;
    ev_tstamp               next_keepalive;
    ev_tstamp               last_keepalive_ack;
    ev_tstamp               last_keepalive_ack_sent;
    ev_io                   io_read;
    ev_io                   io_write;
    ev_timer                io_timeout;

    mlvpn_pkt_buffer_t      hsbuf;

    struct {
        int         running;            // Flag if doing speed test
        int         runningDownload;
        int         runningUpload;
        
        int         downloadDone;       // Flag if speed test has been performed
        uint32_t    downloadRateKbps;   // Estimated download rate

        int         uploadDone;         // Flag if speed test has been performed
        uint32_t    uploadRateKbps;     // Estimated download rate

        uint32_t    max;                // Maximum number of packets to send
        uint32_t    batch;              // Number of packets to send in a batch
        uint16_t    length;             // Length of speedtest packets
        uint16_t    testNumber;
        uint32_t    sequenceNumber;     // Tx sequence number
        uint64_t    txStartTime;        // Start transmit time (msec)
        uint32_t    txTransmitted;      // Number transmitted
        
        uint16_t    tries;              // Tries at reach level
        uint32_t    currentRateKbps;    // Current rate
        uint32_t    maxGoodRateKbps;    // Current highest good rate
        uint32_t    minBadRateKbps;     // Current lowest bad rate
        
        
        uint16_t    downloadTestNumber; 
        uint16_t    downloadTestNumberReport; 
        int         downloadWaitingAck;
        
        uint16_t    uploadTestNumber;
        uint16_t    uploadTestNumberReport; 
        
        uint16_t    rxTestNumber;       // Number of rx speed test
        uint64_t    rxStartTime;        // Time of arrival of first packet
        uint64_t    rxLastTime;         // Time of arrival of last packet packet
        uint32_t    rxNumberPackets;    // Number of packets received
        uint32_t    rxNumberBytes;      // Number of packets received
        
        uint32_t    nextSequenceNumber; // Next expected sequence number
        uint32_t    numberOutOfOrder;   // Number out of order
        uint32_t    numberOld;          // Number old sequence numbers
        uint32_t    numberLost;         // Number lost packets
        
    } speedTest;
    
    uint32_t        remoteSendMaxRateKbps;
    uint32_t        remoteReceiveMeasuredMaxRateKbps;
    uint32_t        tunnelLastSend;
    uint32_t        minTxUsec;
    uint64_t        nextTxTimeUsec;
    uint16_t        rtt;
    uint32_t        initTime;
    
    mlvpnStats_t    stats;
    uint16_t        receiveFirstSeqNum;
    uint16_t        receiveLastSeqNum;

    mlvpnStats_t    clientStats;
    rx_errors_t     dlErrors;
    rx_errors_t     ulErrors;
    uint16_t        clientTapReceiveQueue;
} mlvpn_tunnel_t;

typedef struct  {
    mlvpn_tunnel_t* first;
    mlvpn_tunnel_t* last;
} rtun_list_t;

extern rtun_list_t rtuns;

extern mlvpn_tunnel_t* dslTunnel;
extern mlvpn_tunnel_t* lteTunnel;

//extern struct tuntap_s tuntap;
extern struct ev_loop*    loop;
extern ev_timer reorder_drain_timeout;

/********************************************************************
 * Function Definitions
 *-------------------------------------------------------------------
 */
void usage(char **argv);
int mlvpn_sock_set_nonblocking(int fd);
mlvpn_tunnel_t* mlvpn_rtun_new(
    const char*     name,
    const char*     bindaddr, 
    const char*     bindport, 
    uint32_t        bindfib,
    const char*     destaddr, 
    const char*     destport,
    uint32_t        bandwidth
);
void mlvpn_rtun_drop(mlvpn_tunnel_t *t);
int mlvpn_rtun_bind(mlvpn_tunnel_t *t);
int mlvpn_rtun_start(mlvpn_tunnel_t *t);
void mlvpn_script_get_env(int *env_len, char ***env);
void mlvpn_free_script_env(char **env);
void mlvpn_rtun_status_up(mlvpn_tunnel_t *t);
void mlvpn_rtun_status_down(mlvpn_tunnel_t *t);
void mlvpn_update_status();
void mlvpn_rtun_tick_connect(mlvpn_tunnel_t *t);
void mlvpn_rtun_check_timeout(EV_P_ ev_timer *w, int revents);
void mlvpn_hundred_ms_timeout(EV_P_ ev_timer *w, int revents) ;
void mlvpn_ten_ms_timeout(EV_P_ ev_timer *w, int revents);
void tuntap_io_event_read(EV_P_ ev_io *w, int revents);
void tuntap_io_event_write(EV_P_ ev_io *w, int revents);
void mlvpn_tuntap_init();
void update_process_title();
void mlvpn_quit(EV_P_ ev_signal *w, int revents);
void idle_cb(
    struct ev_loop *loop, 
    ev_idle *w, 
    int revents
);

// Queue packet functions
int mlvpn_queue_put(mlvpn_pkt_buffer_t* queue, mlvpn_pkt_t* pp);
mlvpn_pkt_t* mlvpn_queue_get(mlvpn_pkt_buffer_t* queue);
int mlvpn_queue_count(mlvpn_pkt_buffer_t* queue);

// main function
int main(int argc, char **argv);

#endif
