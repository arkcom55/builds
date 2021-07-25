#ifndef _speedTest_H
#define _speedTest_H

#define SPEEDTEST_TIME_MSEC             (1000.0)
#define SPEEDTEST_INTERVALS_PER_SEC     (100)
#define SPEEDTEST_PKT_PER_INTERVAL_INIT (1)
#define SPEEDTEST_PKT_PER_INTERVAL_INC  (2)
#define SPEEDTEST_BITS_PER_PKT          (8000)
#define SPEEDTEST_MAX_TRIES             (1)
#define SPEEDTEST_INITIAL_RATE_KBPS     (2000)
#define SPEEDTEST_MAX_RATE_KBPS         (50000)
#define SPEEDTEST_RATE_DIFF_THRESHOLD   (10)    // Percent

int SpeedTestStart(mlvpn_tunnel_t* tp);
int SpeedTestDone(mlvpn_tunnel_t* tp);
int SpeedTestAcked(mlvpn_tunnel_t* tp, uint32_t numberPackets, uint32_t numberBytes, uint16_t deltaTime);
int SpeedTestReport(mlvpn_tunnel_t* tp, uint16_t nextSpeedTestNumber);
int SpeedTestInitiate();
int SpeedTestRunUpload(mlvpn_tunnel_t* tp, uint16_t testNumber);
int SpeedTestIsRunning();
int SpeedTestReset(mlvpn_tunnel_t* tp);

#endif