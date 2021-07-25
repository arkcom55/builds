#include "includes.h"

inline uint64_t
mlvpn_timestamp64(ev_tstamp now) {
    uint64_t _now = now * 1000.0;
    return _now;
}

inline uint16_t
mlvpn_timestamp16(uint64_t now) {
    uint16_t ts = now % 65536;
    if (ts == (uint16_t)-1) {
        ts++;
    }
    return ts;
}

inline uint16_t
mlvpn_timestamp16_diff(uint16_t tsnew, uint16_t tsold) {
    int32_t diff = tsnew - tsold;
    if (diff < 0) {
        diff += 65536;
    }
    return diff;
}

inline uint64_t
mlvpn_timestamp_msec() {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    uint64_t timestamp = ts.tv_sec;
    timestamp *= 1000;
    timestamp += (uint64_t)(ts.tv_nsec / 1000000);
    return timestamp;
}

inline uint64_t mlvpn_timestamp_usec() {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    uint64_t timestamp = ts.tv_sec;
    timestamp *= 1000000;
    timestamp += (uint64_t)(ts.tv_nsec / 1000);
    return timestamp;
}
