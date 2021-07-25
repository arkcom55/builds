#ifndef _MLVPN_PKT_H
#define _MLVPN_PKT_H

#include <stdint.h>

/**********************************************************
 * Packet type
 *---------------------------------------------------------
 */
 
typedef struct {
    uint32_t    mlvpnSignature; // MLVPN signature
    uint32_t    dataSeqNum;     // Data sequence number (modulo 2^32)
    uint32_t    reorderSeqNum;  // Reorder sequence number (modulo 2^32)
    uint16_t    tunnelSeqNum;   // Tunnel sequence number (module 2^16)
    uint16_t    messageId;      // Message id
    uint16_t    dataLength;     // Length of user data
    uint8_t     flags;          // Flags
    uint8_t     channel;        // Channel identifier
} __attribute__((packed)) mlvpn_proto_t;

#define MLVPN_MAX_MTU         (1500)
#define MLVPN_IPV4_OVERHEAD   (20)
#define MLVPN_UDP_OVERHEAD    (8)
#define MLVPN_PROTO_OVERHEAD  ((uint16_t)sizeof(mlvpn_proto_t))
#define MLVPN_MTU             (MLVPN_MAX_MTU - MLVPN_IPV4_OVERHEAD - MLVPN_UDP_OVERHEAD - MLVPN_PROTO_OVERHEAD)
#define MLVPM_PKT_MAX_SIZE    (MLVPN_PROTO_OVERHEAD + MLVPN_MTU)

#define MLVPN_MAGIC                     (0x5500)
#define MLVPN_PKT_AUTH                  (MLVPN_MAGIC + 1)
#define MLVPN_PKT_AUTH_OK               (MLVPN_MAGIC + 2)
#define MLVPN_PKT_KEEPALIVE             (MLVPN_MAGIC + 3)
#define MLVPN_PKT_DATA                  (MLVPN_MAGIC + 4)
#define MLVPN_PKT_DISCONNECT            (MLVPN_MAGIC + 5)
#define MLVPN_PKT_CONTROL               (MLVPN_MAGIC + 6)
#define MLVPN_PKT_STATUS_TAP            (MLVPN_MAGIC + 7)
#define MLVPN_PKT_STATUS_TUNNEL         (MLVPN_MAGIC + 8)
#define MLVPN_PKT_KEEPALIVE_RESPONSE    (MLVPN_MAGIC + 9)
#define MLVPN_PKT_SPEEDTEST             (MLVPN_MAGIC + 10)
#define MLVPN_PKT_CLIENT_STATS          (MLVPN_MAGIC + 11)

#define MLVPN_FIRST_MESSAGE_ID          (MLVPN_PKT_AUTH)
#define MLVPN_LAST_MESSAGE_ID           (MLVPN_PKT_CLIENT_STATS)

typedef struct mlvpn_pkt_s { 
    struct mlvpn_pkt_s* previous;
    struct mlvpn_pkt_s* next;
    uint16_t            packetLength;   // Protocol and data
    uint16_t            pad;            // Pad to 32-bits
    mlvpn_proto_t       protocol;
    char                data[MLVPN_MTU];
} __attribute__((packed)) mlvpn_pkt_t;

#define MLVPN_FLAGS_REORDER     (0x01)
#define MLVPN_FLAGS_TCP         (0x02)
#define MLVPN_FLAGS_UDP         (0x04)

/**********************************************************
 * Message structures
 *---------------------------------------------------------
 */
 
typedef struct {
    uint64_t    rttTimestampMsec;    
    struct {
        uint16_t    downloadTestNumber;
        uint16_t    uploadTestNumber;
        uint16_t    uploadBatch;
        uint32_t    uploadMax;
        uint16_t    uploadLength;
    } speedTest;
} mlvpn_pkt_keepalive_t;

typedef struct {
    uint64_t    rttTimestampMsec;
    
    struct {
        uint16_t    uploadTestNumber;
        uint16_t    downloadTestNumber;
        uint32_t    downloadNumberPackets;
        uint32_t    downloadNumberBytes;
        uint16_t    downloadDeltaTime;
    } speedTest;
} mlvpn_pkt_keepalive_response_t;

typedef struct {
    uint64_t    dataTimestampMsec;    
    uint32_t    sendMaxRateKbps;
    uint16_t    tapReceiveQueue;
    
    int32_t     receivedPackets;
    int32_t     receivedPacketsSec;

    int64_t     receivedBytes;
    int32_t     receivedBytesSec;
    
    int32_t     receivedOutOfOrder;
    int32_t     receivedOutOfOrderSec;

    int32_t     receivedOld;
    int32_t     receivedOldSec;

    int32_t     receivedLost;
    int32_t     receivedLostSec;
} mlvpn_pkt_client_stats_t;

typedef struct {
    int32_t     tun1_upload_kbps;
    int32_t     tun2_upload_kbps;
    uint16_t    reorder_timeout_ms;
    uint16_t    stats_timeout_ms;
} mlvpn_pkt_control_t;

typedef struct {
    uint32_t    runTimeSec;
    
    uint32_t    sentPackets;
    uint64_t    sentBytes;
    uint32_t    sentPacketsSec;
    uint32_t    sentBytesSec;
    
    uint32_t    receivedPackets;
    uint64_t    receivedBytes;
    uint32_t    receivedPacketsSec;
    uint32_t    receivedBytesSec;
    
    uint32_t    receivedDiscard;
    uint32_t    receivedDiscardSec;
        
    uint32_t    sentSkipped;
    uint32_t    sentOutOfOrder;
    uint32_t    sentSkippedSec;
    uint32_t    sentOutOfOrderSec;
} mlvpn_pkt_status_tap_t;

typedef struct {
    uint16_t    tunnelIndex;
    uint32_t    runTimeSec;
    uint32_t    sendMaxRateKbps;
    
    uint32_t    sentPackets;
    uint64_t    sentBytes;
    uint32_t    sentPacketsSec;
    uint32_t    sentBytesSec;
    
    uint32_t    receivedPackets;
    uint64_t    receivedBytes;
    uint32_t    receivedPacketsSec;
    uint32_t    receivedBytesSec;
    
    uint32_t    receivedOutOfOrder;
    uint32_t    receivedOld;
    uint32_t    receivedOutOfOrderSec;
    uint32_t    receivedOldSec;
} mlvpn_pkt_status_tunnel_t;

typedef struct {
    uint32_t    sequenceNumber;
    uint16_t    testNumber;
    uint16_t    endFlag;
    char        data[1000 - 8 - MLVPN_PROTO_OVERHEAD];  // 1000 - 8 byte data - proto
} mlvpn_pkt_speedtest_t;

#endif
