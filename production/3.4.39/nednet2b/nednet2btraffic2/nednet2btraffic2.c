//*******************************************************************
// File:  nednet2btraffic2.c
//-------------------------------------------------------------------
// Date:  2021-01-30
// Copyright (c) 2020 Neducation Management Inc.
//-------------------------------------------------------------------
// 1.1-1 2020-07-29 Initial version
//-------------------------------------------------------------------
 
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h> /* read, write, close */
#include <string.h> /* memcpy, memset */
#include <curl/curl.h>
#include <fcntl.h> // Contains file controls like O_RDWR
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "nednet2bcommon.h"

#define DEBUG_SCHEDULE (1)

String gName = "nednet2btraffic2";
String gVersion = "nednet2btraffic2_3.2-107";

// Defaults
#define API_BASE "https://dnsapi.neducation.ca/api/"
#define API_FILE "nedNet2bTraffic.php"

/*
Int64s gRxBytes = -1;
Int64s gTxBytes = -1;
Int64s gSchoolRxBytes = -1;
Int64s gSchoolTxBytes = -1;
Int64s gGeneralRxBytes = -1;
Int64s gGeneralTxBytes = -1;
//struct timeval gLastStatsTime;
*/
Int64s gSchoolBudget = -1;
Int64s gGeneralBudget = -1;

int gNedConnectFileNumber = 0;
int gNedSchoolFileNumber = 0;
int gNedGeneralFileNumber = 0;

String gOrgName = NULL;
String sPhoneNumber = NULL;
Int64s gTotalSchoolBytes = 0;
Int64s gTotalGeneralBytes = 0;
String sSchoolUrl = NULL;
String sStatus = NULL;
String sCarrier = NULL;
String sDDNS = NULL;
String sSerial = NULL;
String sNedMessage = NULL;
String sGroupMessage = NULL;
String sDeviceMessage = NULL;
String sIccid = NULL;
String sImei = NULL;

#define DEFAULT_SAMPLING_INTERVAL       (10)
#define DEFAULT_SAMPLING_COUNT          (6)
#define DEFAULT_INITIAL_REPORT_INTERVAL (60)
#define DEFAULT_REPORT_INTERVAL         (600)

static int sReportInterval = DEFAULT_INITIAL_REPORT_INTERVAL;
static int sNoResponse = 0;
static int sLastReportTime = 0;
static int sSamplingCount = DEFAULT_SAMPLING_COUNT;
static Int64s sLastRxBytesTenSec = -1;
static Int64s sLastTxBytesTenSec = -1;
static Int64s sMaxRxBytesTenSec = 0;
static Int64s sMaxTxBytesTenSec = 0;
static Int64s sLastRxWanBytesTenSec = -1;
static Int64s sLastTxWanBytesTenSec = -1;
static Int64s sMaxRxWanBytesTenSec = 0;
static Int64s sMaxTxWanBytesTenSec = 0;
static int sSplashSchool = 0;
static int sSplashGeneral = 0;
static int sIdleTime = 60;
static int sDnsEnable = 0;
static int sSplashEnable = 0;

char gSSID3[6];

typedef struct TrafficStats_s {
    int    timestamp;
    Int64s rxWanBytes;
    Int64s txWanBytes;
    Int64s rxBytes;
    Int64s txBytes;
    Int64s schoolRxBytes;
    Int64s schoolTxBytes;
    Int64s generalRxBytes;
    Int64s generalTxBytes;
    Int64s rxMaxBps;
    Int64s txMaxBps;
    Int64s rxWanMaxBps;
    Int64s txWanMaxBps;
    int    schoolUsers;
    int    generalUsers;
    struct TrafficStats_s* next;
} TrafficStats_t;

TrafficStats_t* sTrafficStatsStart = NULL;
TrafficStats_t* sTrafficStatsEnd = NULL;

typedef struct Schedule_s {
    String config;
    String category;
    int    timeOfDay;
    String daysOfWeek;
    time_t timeToExecute;
    int    processed;
    struct Schedule_s* next;
} Schedule_t;

Schedule_t* sScheduleStart = NULL;
Schedule_t* sScheduleEnd = NULL;

#define STATS_DSL_RX_BYTES      "/sys/class/net/eth0/statistics/rx_bytes"
#define STATS_DSL_TX_BYTES      "/sys/class/net/eth0/statistics/tx_bytes"
#define STATS_LTE_RX_BYTES      "/sys/class/net/3g-modem_1_1_2/statistics/rx_bytes"
#define STATS_LTE_TX_BYTES      "/sys/class/net/3g-modem_1_1_2/statistics/tx_bytes"

#define STATS_SCHOOL_RX_BYTES   "/sys/class/net/br-lan/statistics/rx_bytes"
#define STATS_SCHOOL_TX_BYTES   "/sys/class/net/br-lan/statistics/tx_bytes"
#define STATS_GENERAL_RX_BYTES  "/sys/class/net/br-guest/statistics/rx_bytes"
#define STATS_GENERAL_TX_BYTES  "/sys/class/net/br-guest/statistics/tx_bytes"

#define STATS_PREFIX            "/sys/class/net/"
#define STATS_RX_SUFFIX         "/statistics/rx_bytes"
#define STATS_TX_SUFFIX         "/statistics/tx_bytes"

static String sStatsDslRxByteFile = NULL;
static String sStatsDslTxByteFile = NULL;
static String sStatsLteRxByteFile = NULL;
static String sStatsLteTxByteFile = NULL;

#define NO_RESPONSE_COUNT (3)

#ifdef TEST
int gTest = 1;
#define NEDCONNECT_TEMPLATE "nedconnectTemplate.html"
#define NEDCONNECT_BASE     "nedconnect"
#define NEDCONNECT_SCHOOL_TEMPLATE     "/nedSchoolTemplate.html"
#define NEDCONNECT_SCHOOL_BASE         "nedSchool"
#define NEDCONNECT_GENERAL_TEMPLATE    "nedGeneralTemplate.html"
#define NEDCONNECT_GENERAL_BASE        "nedGeneral"
#define NEDCONNECT_TRAFFIC_CONFIG      "nedTraffic.json"
#else
int gTest = 0;
#define NEDCONNECT_TEMPLATE "/root/nedconnectTemplate.html"
#define NEDCONNECT_BASE     "/tmp/nedconnect"
#define NEDCONNECT_SCHOOL_TEMPLATE     "/root/nedSchoolTemplate.html"
#define NEDCONNECT_SCHOOL_BASE         "/tmp/nedSchool"
#define NEDCONNECT_GENERAL_TEMPLATE    "/root/nedGeneralTemplate.html"
#define NEDCONNECT_GENERAL_BASE        "/tmp/nedGeneral"
#define NEDCONNECT_TRAFFIC_CONFIG      "/root/nedTraffic.json"
#endif

#define CONFIG_DNS_SCHOOL       "dnsSchool"
#define CONFIG_DNS_GENERAL      "dnsGeneral"
#define CONFIG_DHCP             "/etc/config/dhcp"
#define CONFIG_NO_SPLASH        "noSplash"
#define CONFIG_REPORT_INTERVAL  "reportInterval"
#define CONFIG_IDLE_TIME        "idleTime"

#define FREE(rp) {if (rp) {free(rp); rp = NULL;}}

//*******************************************************************
// WiFi Usage
//-------------------------------------------------------------------
#define WRTBWMON_FILE    "/tmp/usage.db"

typedef struct Wrtbwmon_s {
    char     mac[20];
    char     ip[20];
    char     iface[40];
    Int64s   in;
    Int64s   out;
    Int64s   total;
    char     firstDate[30];
    char     lastDate[30];
    time_t   firstTimestamp;
    time_t   lastTimestamp;
    struct   Wrtbwmon_s* next;
} Wrtbwmon_t;

Wrtbwmon_t* sLastWiFiUsage = NULL;

//*******************************************************************
// Template structure
//-------------------------------------------------------------------
typedef struct {
    char    key[80];
    char    value[80];
} TemplateDict_t;

//*******************************************************************
// Template structure
//-------------------------------------------------------------------
static int sMlvpnStarted = FALSE;
static int sMlvpnIndex = 0;
static String sMlvpnServerIP = NULL;
static int sMlvpnEnabled = 0;

//*******************************************************************
// ParseWrtbwmon
//-------------------------------------------------------------------
Wrtbwmon_t* ParseWrtbwmon(String bp) {
    Wrtbwmon_t* p = NULL;
    String sp;
    
    //Logger(LOG_DEBUG, "nednet2btraffic2: Parsing '%s'", bp);
    
    // Skip comment lines
    if (bp[0] == '#') {
        goto errorExit;
    }
    
    p = malloc(sizeof(*p));
    if (!p) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not allocate usage structure");
        goto errorExit;
    }
    memset(p, 0, sizeof(*p));
    
    sp = strtok(bp, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse mac");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->mac, sp, sizeof(p->mac) - 1);
    
    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse ip");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->ip, sp, sizeof(p->ip) - 1);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse iface");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->iface, sp, sizeof(p->iface) - 1);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse in");
        free(p);
        p = NULL;
        goto errorExit;
    }
    p->in = atoll(sp);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse out");
        free(p);
        p = NULL;
        goto errorExit;
    }
    p->out = atoll(sp);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse total");
        free(p);
        p = NULL;
        goto errorExit;
    }
    p->total = atoll(sp);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse first_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->firstDate, sp, sizeof(p->firstDate) - 1);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not parse last_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->lastDate, sp, sizeof(p->lastDate) - 1);

    // Convert dates to timestamp values
    p->firstTimestamp = ConvertDate(p->firstDate);
    if (p->firstTimestamp == (time_t)(-1)) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not convert first_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
    
    p->lastTimestamp = ConvertDate(p->lastDate);
    if (p->lastTimestamp == (time_t)(-1)) {
        Logger(LOG_ERROR, "nednet2btraffic2: Could not convert last_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
        
  errorExit:
    return p;
}

//*******************************************************************
// GetUsage
//-------------------------------------------------------------------
Wrtbwmon_t* GetUsage() {
    Wrtbwmon_t* start = NULL;
    Wrtbwmon_t* end = NULL;
    Wrtbwmon_t* entry;
    Wrtbwmon_t* p;
    FILE* fp = NULL;
    char buf[256];
    String response;
    
    if (gTest) {
        char buf[256];
        strcpy(buf, "0c:9d:92:b6:23:4e,192.168.8.228,br-lan,3316424,6292776,9609200,04-08-2020_19:37:03,04-08-2020_19:39:17\n");
        return ParseWrtbwmon(buf);
    }
    
    fp = fopen(WRTBWMON_FILE, "r");
    if (!fp) {
        //Logger(LOG_ERROR, "nednet2btraffic2: Could not open usage db");
        goto errorExit;
    }
    
    while (fgets(buf, sizeof(buf), fp)) {
        entry = ParseWrtbwmon(buf);
        if (entry) {
            if (end) {
                end->next = entry;
                end = entry;
            } else {
                start = end = entry;
            }
        }
    }
    
  errorExit:
    if (fp) {
        fclose(fp);
    }
    return start;
}

//*******************************************************************
// FreeUsage
//-------------------------------------------------------------------
int FreeUsage(Wrtbwmon_t* p) {
    while (p) {
        Wrtbwmon_t* next = p->next;
        free(p);
        p = next;
    }
    return ERR_OK;
}

//*******************************************************************
// rxBytes
//-------------------------------------------------------------------
int rxBytes(TrafficStats_t* tsp) {
#ifdef TEST
    return ERR_OK;
#else
    int    status = ERR_ERROR;
    FILE*   fp = NULL;
    char    buffer[40];
    
    fp = fopen(sStatsDslRxByteFile, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open wan rx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read rx stats file");
        goto errorExit;
    }
    tsp->rxWanBytes = atoll(buffer);
    fclose(fp);
    fp = NULL;
    
    fp = fopen(sStatsLteRxByteFile, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open rx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read rx stats file");
        goto errorExit;
    }
    tsp->rxBytes = atoll(buffer);
    fclose(fp);
    fp = NULL;
    
    fp = fopen(STATS_SCHOOL_RX_BYTES, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open school rx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read school rx stats file");
        goto errorExit;
    }
    tsp->schoolRxBytes = atoll(buffer);
    fclose(fp);
    fp = NULL;
    
    fp = fopen(STATS_GENERAL_RX_BYTES, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open general rx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read general rx stats file");
        goto errorExit;
    }
    tsp->generalRxBytes = atoll(buffer);

    status = ERR_OK;
    
  errorExit:
    if (fp) {
        fclose(fp);
    }
    return status;
#endif
}

//*******************************************************************
// txBytes
//-------------------------------------------------------------------
int txBytes(TrafficStats_t* tsp) {
#ifdef TEST
    return ERR_OK;
#else
    int    status = ERR_ERROR;
    FILE*   fp = NULL;
    char    buffer[40];
    
    fp = fopen(sStatsDslTxByteFile, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open wan tx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read wan tx stats file");
        goto errorExit;
    }
    tsp->txWanBytes = atoll(buffer);
    fclose(fp);
    fp = NULL;
    
    fp = fopen(sStatsLteTxByteFile, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open tx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read tx stats file");
        goto errorExit;
    }
    tsp->txBytes = atoll(buffer);
    fclose(fp);
    fp = NULL;
    
    fp = fopen(STATS_SCHOOL_TX_BYTES, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open school tx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read school tx stats file");
        goto errorExit;
    }
    tsp->schoolTxBytes = atoll(buffer);
    fclose(fp);
    fp = NULL;
    
    fp = fopen(STATS_GENERAL_TX_BYTES, "r");
    if (!fp) {
        Logger(LOG_DEBUG, "Could not open general tx stats file");
        goto errorExit;
    }
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        Logger(LOG_DEBUG, "Could not read general tx stats file");
        goto errorExit;
    }
    tsp->generalTxBytes = atoll(buffer);

    status = ERR_OK;
    
  errorExit:
    if (fp) {
        fclose(fp);
    }
    return status;
#endif
}

//*******************************************************************
// ConfigGetBase
//-------------------------------------------------------------------
String ConfigGetBase(String config) {
    String bp = NULL;
    char base[40];
    
#if DEBUG_SCHEDULE > 0    
    Logger(LOG_DEBUG, "ConfigGetBase:Entry: '%s'", config);
#endif

    if (!config) {
        goto errorExit;
    }
    
    // Extract base name for config
    String p = strstr(config, ".");
    if (!p) {
        goto errorExit;
    }
    int baseLength = (int)(p - config);
    if (baseLength < 1) {
        goto errorExit;
    }
    strncpy(base, config, baseLength);
    base[baseLength] = 0;

    bp = StrDuplicate(base);
    
  errorExit:
#if DEBUG_SCHEDULE > 0    
    Logger(LOG_DEBUG, "ConfigGetBase:Status: '%s'", bp ? bp : "<NULL>");
#endif
    return bp;
}

//*******************************************************************
// ConfigAlreadySet
//-------------------------------------------------------------------
int ConfigAlreadySet(String config) {
    String configContent = NULL;
    int status = FALSE;
    char path[60];
    String bp = NULL;
    
#if DEBUG_SCHEDULE > 0    
    Logger(LOG_DEBUG, "ConfigAlreadySet:Entry: '%s'", config);
#endif

    // Extract base name for config
    bp = ConfigGetBase(config);
    
    // Read in file
    sprintf(path, "/root/config/%s", bp);
    configContent = ReadTextFile(path);
    if (!configContent) {
        goto errorExit;
    }
    
    if (!strcmp(configContent, config)) {
        status = TRUE;
    }
    
  errorExit:
    if (bp) {
        free(bp);
    }
    if (configContent) {
        free(configContent);
    }
#if DEBUG_SCHEDULE > 0    
    Logger(LOG_DEBUG, "ConfigAlreadySet:Status %d", status);
#endif
    return status;
}

//*******************************************************************
// ConfigSetConfig
//-------------------------------------------------------------------
int ConfigSetConfig(String config) {
    int status = ERR_ERROR;
    char base[40];
    char path[60];
    FILE* fp = NULL;
    String bp = NULL;

#if DEBUG_SCHEDULE > 0    
    Logger(LOG_DEBUG, "ConfigSetConfig:Entry: '%s'", config);
#endif

    // Check for config directory
    DIR* dir = opendir("/root/config");
    if (!dir)
    {
        /* Create directory. */
        mkdir("/root/config", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
    
    // Extract base name for config and create path
    bp = ConfigGetBase(config);
    sprintf(path, "/root/config/%s", bp);
    
    // Write file
    fp = fopen(path, "w");
    if (!fp) {
        goto errorExit;
    }
    fputs(config, fp);
    
    status = ERR_OK;
    
  errorExit:  
    if (bp) {
        free(bp);
    }
    if (fp) {
        fclose(fp);
    }
#if DEBUG_SCHEDULE > 0    
    Logger(LOG_DEBUG, "ConfigSetConfig:Status %d", status);
#endif
    return status;
}

//*******************************************************************
// ScheduleCalculateTime
//-------------------------------------------------------------------
time_t ScheduleCalculateTime(Schedule_t* sp) {
    time_t xtime = 0;
    
    // Calculate time at start of today
    time_t ctime = time(NULL);
    struct tm* info = localtime(&ctime);
    info->tm_hour = 0;
    info->tm_min = 0;
    info->tm_sec = 0;
    xtime = mktime(info);
    
    // Include offset
    xtime += sp->timeOfDay;
    
    return xtime;
}

//*******************************************************************
// ScheduleCalculateNextTime
//-------------------------------------------------------------------
time_t ScheduleCalculateNextTime(Schedule_t* sp) {
    time_t xtime = ScheduleCalculateTime(sp);
    
    // Check if already past
    if (xtime < time(NULL)) {
        // Add one day
        xtime += 24 * 60 * 60;
    }
    
    return xtime;
}

//*******************************************************************
// Message: TrafficStats
//-------------------------------------------------------------------
int MessageTrafficStats() {
    int             status = ERR_ERROR;
    Message_t*      mp = NULL;
    TrafficStats_t* tsp = NULL;
    Wrtbwmon_t*     wiFiUsage = NULL;
    
    Parameters_t    parameters;
    memset(&parameters, 0, sizeof(parameters));
    
    Logger(LOG_DEBUG, "Message TrafficStats2");

    // Get stats
    int ctime = time(NULL);
    
    tsp = malloc(sizeof(*tsp));
    if (!tsp) {
        goto errorExit;
    }
    memset(tsp, -1, sizeof(*tsp));
    tsp->timestamp = ctime;
    tsp->next = NULL;

    rxBytes(tsp);
    txBytes(tsp);
    
    if (sLastRxBytesTenSec >= 0 && sLastTxBytesTenSec >= 0) {
        Int64s rxDelta = tsp->rxBytes - sLastRxBytesTenSec;
        if (rxDelta > sMaxRxBytesTenSec) {
            sMaxRxBytesTenSec = rxDelta;
        }
        
        Int64s txDelta = tsp->txBytes - sLastTxBytesTenSec;
        if (txDelta > sMaxTxBytesTenSec) {
            sMaxTxBytesTenSec = txDelta;
        }
    }
    sLastRxBytesTenSec = tsp->rxBytes;
    sLastTxBytesTenSec = tsp->txBytes;
    
    if (sLastRxWanBytesTenSec >= 0 && sLastTxWanBytesTenSec >= 0) {
        Int64s rxDelta = tsp->rxWanBytes - sLastRxWanBytesTenSec;
        if (rxDelta > sMaxRxWanBytesTenSec) {
            sMaxRxWanBytesTenSec = rxDelta;
        }
        
        Int64s txDelta = tsp->txWanBytes - sLastTxWanBytesTenSec;
        if (txDelta > sMaxTxWanBytesTenSec) {
            sMaxTxWanBytesTenSec = txDelta;
        }
    }
    sLastRxWanBytesTenSec = tsp->rxWanBytes;
    sLastTxWanBytesTenSec = tsp->txWanBytes;
    
    if (++sSamplingCount < DEFAULT_SAMPLING_COUNT) {
        goto okExit;
    }
    
    sSamplingCount = 0;
    tsp->rxMaxBps = (sMaxRxBytesTenSec * 8) / 10;
    tsp->txMaxBps = (sMaxTxBytesTenSec * 8) / 10;
    sMaxRxBytesTenSec = 0;
    sMaxTxBytesTenSec = 0;
    tsp->rxWanMaxBps = (sMaxRxWanBytesTenSec * 8) / 10;
    tsp->txWanMaxBps = (sMaxTxWanBytesTenSec * 8) / 10;
    sMaxRxWanBytesTenSec = 0;
    sMaxTxWanBytesTenSec = 0;

    //*******************************************
    // Determine active users on each WiFi
    //-------------------------------------------
    tsp->schoolUsers = 0;
    tsp->generalUsers = 0;
    wiFiUsage = GetUsage();
    if (sLastWiFiUsage && wiFiUsage) {
        // Step through current usage
        for (Wrtbwmon_t* cp = wiFiUsage; cp; cp = cp->next) {
            // Look for match in previous usage
            Wrtbwmon_t* pp;
            for (pp = sLastWiFiUsage; pp; pp = pp->next) {
                if (!strcmp(cp->mac, pp->mac) && !strcmp(cp->iface, pp->iface)) {
                    break;
                }
            }
            if (pp) {
                // Check if change in total data
                if (cp->total != pp->total) {
                    // Change in data
                    if (!strcmp(cp->iface, "br-lan")) {
                        tsp->schoolUsers++;
                    } else {
                        tsp->generalUsers++;
                    }
                }
            } else {
                // New user
                if (!strcmp(cp->iface, "br-lan")) {
                    tsp->schoolUsers++;
                } else {
                    tsp->generalUsers++;
                }
            }
        }
    }
    sLastWiFiUsage = wiFiUsage;
    wiFiUsage = NULL;
    
    //*******************************************
    // Save stats
    //-------------------------------------------
    if (sTrafficStatsEnd) {
        sTrafficStatsEnd->next = tsp;
        sTrafficStatsEnd = tsp;
    } else {
        sTrafficStatsStart = sTrafficStatsEnd = tsp;
    }
    tsp = NULL;
    
    // Check if time to send
    int now = FALSE;
    if (access("/tmp/trafficNow", F_OK) == 0) {
        now = TRUE;
        remove("/tmp/trafficNow");
    }
    if (!now && sLastReportTime && ctime < (sLastReportTime + sReportInterval)) {
        goto okExit;
    }
    
    ParametersInit(&parameters);
    
    if (ParametersAddInt(&parameters, "timestamp", ctime)) {
        goto errorExit;
    }
    
    int count = 0;
    char arrayName[30];
    for (tsp = sTrafficStatsStart; tsp; tsp = tsp->next) {
        sprintf(arrayName, "trafficStats%d", count++);
        if (ParametersAddArray(&parameters, arrayName)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "timestamp", (Int64s)tsp->timestamp)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "rxBytes", tsp->rxBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "txBytes", tsp->txBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "rxWanBytes", tsp->rxWanBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "txWanBytes", tsp->txWanBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "schoolRxBytes", tsp->schoolRxBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "schoolTxBytes", tsp->schoolTxBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "generalRxBytes", tsp->generalRxBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "generalTxBytes", tsp->generalTxBytes)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "maxRxBps", tsp->rxMaxBps)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "maxTxBps", tsp->txMaxBps)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "maxRxWanBps", tsp->rxWanMaxBps)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "maxTxWanBps", tsp->txWanMaxBps)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "schoolUsers", (Int64s)tsp->schoolUsers)) {
            goto errorExit;
        }
        if (ParametersAddLongToArray(&parameters, arrayName, "generalUsers", (Int64s)tsp->generalUsers)) {
            goto errorExit;
        }
    }
    
    if (ParametersAddLong(&parameters, "memory", GetCurrentRSS())) {
        goto errorExit;
    }
    
    mp = MessageConstruct("trafficStats2RawExtended", &parameters);
    if (!mp) {
        goto errorExit;
    }
    
    Logger(LOG_DEBUG, "Sending trafficStats2RawExtended");
    if (sNoResponse < NO_RESPONSE_COUNT) {
        sNoResponse++;
    }
    if (MessagePost(mp, TRUE)) {
        goto errorExit;
    }
    sLastReportTime = ctime;
    
    // Free traffic stats
    for (tsp = sTrafficStatsStart; tsp; ) {
        TrafficStats_t* tsp2 = tsp->next;
        free(tsp);
        tsp = tsp2;
    }
    sTrafficStatsStart = sTrafficStatsEnd = NULL;    
    
  okExit:
    status = ERR_OK;
    
  errorExit:
    ParametersFree(&parameters);
    if (mp) {
        free(mp);
    }
    if (tsp) {
        free(tsp);
    }
    if (wiFiUsage) {
        FreeUsage(wiFiUsage);
    }
    return status;
}

//*******************************************************************
// GetStatus()
//-------------------------------------------------------------------
String GetStatus() {
    String status = NULL;
    String response = NULL;
    char buf[80];
    
    sprintf(buf, "Carrier: %s<br>", sCarrier ? sCarrier : "<Unknown>");
    status = StrConcat(status, buf);
    sprintf(buf, "DDNS: %s<br>", sDDNS ? sDDNS : "<Unknown>");
    status = StrConcat(status, buf);
    sprintf(buf, "S/N: %s<br>", sSerial ? sSerial : "<Unknown>");
    status = StrConcat(status, buf);
    ExecuteCommand("opkg info nednet2b\\*", &response);
    if (response) {
        //Logger(LOG_DEBUG, "opkg info %s", response);
        String bp = response;
        String sp2;
        
        while (sp2 = strtok(bp, "\r\n")) {
            bp = NULL;
            if (strstr(sp2, "Package") || strstr(sp2, "Version")) {
                status = StrConcat(status, sp2);
                status = StrConcat(status, "<br>");
            }
        }
        free(response);
    }
    
    return status;
}

//*******************************************************************
// UpdateTemplateGeneral
//-------------------------------------------------------------------
int UpdateTemplateGeneral(String inFileName, String outFileName, TemplateDict_t* tp, int tcount) {
    FILE* fpIn = NULL;
    FILE* fpOut = NULL;
    char buffer[2048];
    char outFile[80];
    
    fpIn = fopen(inFileName, "r");
    if (!fpIn) {
        Logger(LOG_ERROR, "Unable to read template file '%s'", inFileName);
        goto errorExit;
    }
    
    fpOut = fopen(outFileName, "w");
    if (!fpOut) {
        Logger(LOG_ERROR, "Unable to open output file '%s'", outFile);
        goto errorExit;
    }
    
    while (fgets(buffer, sizeof(buffer), fpIn)) {
        char buf2[2048];
        String sp;
        
        strcpy(buf2, buffer);
   
        for (int i = 0; i < tcount; i++) {
            sp = strstr(buffer, tp[i].key);
            if (sp) {
                strncpy(buf2, buffer, (int)(sp - buffer));
                buf2[(int)(sp - buffer)] = 0;
                strcat(buf2, tp[i].value);
                strcat(buf2, &buffer[(int)(sp - buffer + strlen(tp[i].key))]);
                strcpy(buffer, buf2);
            }
        }
        fputs(buf2, fpOut);
    }
    fclose(fpOut);
    fpOut = NULL;

  errorExit:
    if (fpOut) {
        fclose(fpOut);
    }
    if (fpIn) {
        fclose(fpIn);
    }
    return 0;
}

//*******************************************************************
// UpdateTemplate
//-------------------------------------------------------------------
int UpdateTemplate(String inFileName, String outFileBaseName, int* index) {
    FILE* fpIn = NULL;
    FILE* fpOut = NULL;
    char buffer[2048];
    char outFile[80];
    
    fpIn = fopen(inFileName, "r");
    if (!fpIn) {
        Logger(LOG_ERROR, "Unable to read template file '%s'", inFileName);
        goto errorExit;
    }
    
    if (index) {
        sprintf(outFile, "%s_%d.html", outFileBaseName, *index);
        if (++(*index) > 1) {
            *index = 0;
        }
    } else {
        sprintf(outFile, "%s_0.html", outFileBaseName);
    }
    fpOut = fopen(outFile, "w");
    if (!fpOut) {
        Logger(LOG_ERROR, "Unable to open output file '%s'", outFile);
        goto errorExit;
    }
    
    while (fgets(buffer, sizeof(buffer), fpIn)) {
        char buf2[2048];
        String sp;
        
        strcpy(buf2, buffer);
        
        sp = strstr(buffer, "^orgName^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (gOrgName && strlen(gOrgName)) {
                strcat(buf2, gOrgName);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^orgName^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^mac^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (gMAC && strlen(gMAC)) {
                strcat(buf2, gMAC);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^mac^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^phone^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            strcat(buf2, sPhoneNumber);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^phone^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^iccid^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sIccid) {
                strcat(buf2, sIccid);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^iccid^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^imei^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sImei) {
                strcat(buf2, sImei);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^imei^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^stats^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            char sbuf[128];
            
            if (gTotalSchoolBytes || gTotalGeneralBytes) {
                int school = (int)(gTotalSchoolBytes / 1000000LL);
                float schoolGB = (float)(school) / 1000.0;
                float schoolBudget = ((float)(gSchoolBudget / 1000000LL)) / 1000.0;
                int general = (int)(gTotalGeneralBytes / 1000000LL);
                float generalGB = (float)(general) / 1000.0;
                float generalBudget = ((float)(gGeneralBudget / 1000000LL)) / 1000.0;
                sprintf(sbuf, "School: %.3f/%.3f GB, General: %.3f/%.3f GB", schoolGB, schoolBudget, generalGB, generalBudget);
                strcat(buf2, sbuf);
            }
            
            sprintf(sbuf, "<br>Phone: %s<br>MAC: %s", sPhoneNumber, gMAC);
            strcat(buf2, sbuf);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^stats^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^ssid3^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            strcat(buf2, gSSID3);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^ssid3^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^schoolUrl^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sSchoolUrl && strlen(sSchoolUrl)) {
                strcat(buf2, sSchoolUrl);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^schoolUrl^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^nedMessage^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sNedMessage && strlen(sNedMessage)) {
                strcat(buf2, sNedMessage);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^nedMessage^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^groupMessage^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sGroupMessage && strlen(sGroupMessage)) {
                strcat(buf2, sGroupMessage);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^groupMessage^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^deviceMessage^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sDeviceMessage && strlen(sDeviceMessage)) {
                strcat(buf2, sDeviceMessage);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^deviceMessage^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^totalSchool^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            char sbuf[128];
            sprintf(sbuf, "%.1f", ((float)(gTotalSchoolBytes / 1000000LL)) / 1000.0);
            strcat(buf2, sbuf);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^totalSchool^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^totalGeneral^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            char sbuf[128];
            sprintf(sbuf, "%.1f", ((float)(gTotalGeneralBytes / 1000000LL)) / 1000.0);
            strcat(buf2, sbuf);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^totalGeneral^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^budgetSchool^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            char sbuf[128];
            sprintf(sbuf, "%.1f", ((float)(gSchoolBudget / 1000000LL)) / 1000.0);
            strcat(buf2, sbuf);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^budgetSchool^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^budgetGeneral^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            char sbuf[128];
            sprintf(sbuf, "%.1f", ((float)(gGeneralBudget / 1000000LL)) / 1000.0);
            strcat(buf2, sbuf);
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^budgetGeneral^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^percentageSchool^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (gSchoolBudget) {
                char sbuf[128];
                sprintf(sbuf, "%d", (int)round(((float)gTotalSchoolBytes * 100.0) / ((float)gSchoolBudget)));
                strcat(buf2, sbuf);
            } else {
                strcat(buf2, "0");
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^percentageSchool^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^percentageGeneral^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (gGeneralBudget) {
                char sbuf[128];
                sprintf(sbuf, "%d", (int)round(((float)gTotalGeneralBytes * 100.0) / ((float)gGeneralBudget)));
                strcat(buf2, sbuf);
            } else {
                strcat(buf2, "0");
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^percentageGeneral^"))]);
            strcpy(buffer, buf2);
        }
        
        sp = strstr(buffer, "^status^");
        if (sp) {
            strncpy(buf2, buffer, (int)(sp - buffer));
            buf2[(int)(sp - buffer)] = 0;
            
            if (sStatus) {
                strcat(buf2, sStatus);
            }
            
            strcat(buf2, &buffer[(int)(sp - buffer + strlen("^status^"))]);
            strcpy(buffer, buf2);
        }
        
        fputs(buf2, fpOut);
    }
    fclose(fpOut);
    fpOut = NULL;
    
    sprintf(buffer, "ln -s -f %s %s.html", outFile, outFileBaseName);
    system(buffer);
    
  errorExit:
    if (fpOut) {
        fclose(fpOut);
    }
    if (fpIn) {
        fclose(fpIn);
    }
    return 0;
}

//*******************************************************************
// ExecuteConfig
//-------------------------------------------------------------------
int ExecuteConfig(String config) {
    if (config) {
        // Check if not already set
        if (!ConfigAlreadySet(config)) {
            char command[256];
            sprintf(command, "cd /root;chmod 4755 %s;./%s > %s.out 2>&1", config, config, config);
            Logger(LOG_DEBUG, "main Executing command: '%s'", command);  
            ExecuteCommand(command, NULL);
            ConfigSetConfig(config);
        }
    }
    return ERR_OK;
}

//*******************************************************************
// DnsUpdate
//-------------------------------------------------------------------
int DnsUpdate() {
    int err = ERR_ERROR;
    String cp = NULL;
    String dnsSchool = NULL;
    String dnsGeneral = NULL;
    String dnsSchool2 = NULL;
    String dnsGeneral2 = NULL;
    String command = NULL;
    
    // Read in DHCP file
    cp = ReadTextFile(CONFIG_DHCP);
    if (!cp) {
        Logger(LOG_ERROR, "DnsUpdate:  Error reading DHCP file");
        goto errorExit;
    }
    
    // Get DNS values
    dnsSchool = ConfigRead(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_DNS_SCHOOL);
    Logger(LOG_INFO, "DnsUpdate: dnsSchool '%s'", dnsSchool);
    if (dnsSchool && strlen(dnsSchool)) {
        dnsSchool2 = StrDuplicate(dnsSchool);
        
        // Parse on comma
        int found = TRUE;
        for (String sp = strtok(dnsSchool, ","); sp; sp = strtok(NULL, ",")) {
            if (!strstr(cp, sp)) {
                found = FALSE;
                break;
            }
        }
        
        if (!found) {
            command = StrConcat(command, "uci delete dhcp.lan_dns.server;");
            for (String sp = strtok(dnsSchool2, ","); sp; sp = strtok(NULL, ",")) {
                command = StrConcat(command, "uci add_list dhcp.lan_dns.server=");
                command = StrConcat(command, sp);
                command = StrConcat(command, ";");
            }
        }
    }
    
    dnsGeneral = ConfigRead(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_DNS_GENERAL);
    Logger(LOG_INFO, "DnsUpdate: dnsGeneral '%s'", dnsGeneral);
    if (dnsGeneral && strlen(dnsGeneral)) {
        dnsGeneral2 = StrDuplicate(dnsGeneral);
        
        // Parse on comma
        int found = TRUE;
        for (String sp = strtok(dnsGeneral, ","); sp; sp = strtok(NULL, ",")) {
            if (!strstr(cp, sp)) {
                found = FALSE;
                break;
            }
        }
        
        if (!found) {
            command = StrConcat(command, "uci delete dhcp.guest_dns.server;");
            for (String sp = strtok(dnsGeneral2, ","); sp; sp = strtok(NULL, ",")) {
                command = StrConcat(command, "uci add_list dhcp.guest_dns.server=");
                command = StrConcat(command, sp);
                command = StrConcat(command, ";");
            }
        }
    }
    
    if (command) {
        command = StrConcat(command, "uci commit dhcp;/etc/init.d/dnsmasq restart > /tmp/dnsmasq.restart 2>&1");
        Logger(LOG_INFO, "DnsUpdate: '%s'", command);
        ExecuteCommand(command, NULL);
    }
    
    err = ERR_OK;
    
  errorExit:
    if (command) {
        free(command);
    }
    if (dnsGeneral) {
        free(dnsGeneral);
    }
    if (dnsSchool) {
        free(dnsSchool);
    }
    if (dnsGeneral2) {
        free(dnsGeneral2);
    }
    if (dnsSchool2) {
        free(dnsSchool2);
    }
    if (cp) {
        free(cp);
    }
    return err;
}

//*******************************************************************
// SetIpTablesNat
//-------------------------------------------------------------------
int SetIpTablesNat(int position, String subcommand) {
    Logger(LOG_DEBUG, "SetIpTablesNat: %d:'%s'", position, subcommand);
    int found = FALSE;
    char buf[128];
    int status;
    
    for (int i = 0; i <= 3; i++) {
        String response;
        
        // Check if already in table
        sprintf(buf, "iptables -t nat -C PREROUTING %s", subcommand);
        response = NULL;
        status = ExecuteCommand(buf, &response);
        if (response) {
            Logger(LOG_DEBUG, "SetIpTablesNat Check Response: '%s'", response);            
            free(response);
        }
        if (status == 0) {
            found = TRUE;
            break;
        }
        
        // Attempt to add
        sprintf(buf, "iptables -t nat -I PREROUTING %d %s", position, subcommand);
        response = NULL;
        ExecuteCommand(buf, &response);
        if (response) {
            Logger(LOG_DEBUG, "SetIpTablesNat Insert Response: '%s'", response);            
            free(response);
        }
    }
    
    if (!found) {
        Logger(LOG_ERROR, "SetIpTablesNat: FAILED %d:'%s'", position, subcommand);
    }
    
    return ERR_OK;
}

//*******************************************************************
// SetSplash
//-------------------------------------------------------------------
int SetSplash(int schoolRequest, int generalRequest) {
    int err = ERR_ERROR;
    
    // Determine if should be off or on
    String noSplash = ConfigRead(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_NO_SPLASH);
    if (noSplash) {
        schoolRequest = 0;
        generalRequest = 0;
    }
    
    if (schoolRequest != sSplashSchool) {
        if (schoolRequest) {
            ExecuteCommand("iptables -t nat -A PREROUTING -i br-lan -s 0/0 -p tcp --dport 80 -j DNAT --to 192.168.8.1:81", NULL);
        } else {
            ExecuteCommand("iptables -t nat -D PREROUTING -i br-lan -s 0/0 -p tcp --dport 80 -j DNAT --to 192.168.8.1:81", NULL);
        }
        sSplashSchool = schoolRequest;
    }
    
    if (generalRequest != sSplashGeneral) {
        if (generalRequest) {
            ExecuteCommand("iptables -t nat -A PREROUTING -i br-guest -s 0/0 -p tcp --dport 80 -j DNAT --to 192.168.9.1:82", NULL);
            //ExecuteCommand("iptables -t nat -A PREROUTING -i br-guest -s 0/0 -p tcp --dport 443 -j DNAT --to 192.168.9.1:444", NULL);
        } else {
            ExecuteCommand("iptables -t nat -D PREROUTING -i br-guest -s 0/0 -p tcp --dport 80 -j DNAT --to 192.168.9.1:82", NULL);
            //ExecuteCommand("iptables -t nat -D PREROUTING -i br-guest -s 0/0 -p tcp --dport 443 -j DNAT --to 192.168.9.1:82", NULL);
        }
        sSplashGeneral = generalRequest;
    }
    
    err = ERR_OK;
    return err;
}

//*******************************************************************
// PostResponse
//-------------------------------------------------------------------
static int PostResponse(Response_t* response) {
    if (!strcmp(response->messageId, "trafficStats2Raw") || !strcmp(response->messageId, "trafficStats2RawExtended")) {
        Logger(LOG_DEBUG, "Received trafficStats2 response");
        sNoResponse = 0;
        String reportInterval = ConfigRead(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_REPORT_INTERVAL);
        if (reportInterval && strlen(reportInterval)) {
            sReportInterval = atoi(reportInterval);
        } else {
            sReportInterval = DEFAULT_REPORT_INTERVAL;
        }
        if (sReportInterval < 60) {
            sReportInterval = 60;
        }
        
        Int64s schoolTargetKbps = 0;
        Int64s generalTargetKbps = 0;
        Int64s schoolOBkbps = 0;
        Int64s generalOBkbps = 0;
        int mlvpnFound = 0;
        for (cJSON* item = response->data; item; item = item->next) {
            Logger(LOG_DEBUG, "Item key '%s', type=%d", item->string, item->type);
            if (!strcmp(item->string, "orgName")) {
                StrReplace(&gOrgName, item->valuestring);
            } else if (!strcmp(item->string, "totalSchool")) {
                gTotalSchoolBytes = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "totalGeneral")) {
                gTotalGeneralBytes = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "schoolBudget")) {
                gSchoolBudget = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "generalBudget")) {
                gGeneralBudget = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "phone")) {
                StrReplace(&sPhoneNumber, item->valuestring);
            } else if (!strcmp(item->string, "iccid")) {
                StrReplace(&sIccid, item->valuestring);
            } else if (!strcmp(item->string, "imei")) {
                StrReplace(&sImei, item->valuestring);
            } else if (!strcmp(item->string, "schoolUrl")) {
                StrReplace(&sSchoolUrl, item->valuestring);
            } else if (!strcmp(item->string, "carrier")) {
                StrReplace(&sCarrier, item->valuestring);
            } else if (!strcmp(item->string, "ddns")) {
                StrReplace(&sDDNS, item->valuestring);
            } else if (!strcmp(item->string, "serial")) {
                StrReplace(&sSerial, item->valuestring);
            } else if (!strcmp(item->string, "nedMessage")) {
                StrReplace(&sNedMessage, item->valuestring);
            } else if (!strcmp(item->string, "groupMessage")) {
                StrReplace(&sGroupMessage, item->valuestring);
            } else if (!strcmp(item->string, "deviceMessage")) {
                StrReplace(&sDeviceMessage, item->valuestring);
            } else if (!strcmp(item->string, "schoolTargetKbps")) {
                schoolTargetKbps = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "generalTargetKbps")) {
                generalTargetKbps = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "schoolOBkbps")) {
                schoolOBkbps = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "generalOBkbps")) {
                generalOBkbps = (Int64s)(item->valuedouble);
            } else if (!strcmp(item->string, "dnsSchool")) {
                ConfigWrite(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_DNS_SCHOOL, item->valuestring);
            } else if (!strcmp(item->string, "dnsGeneral")) {
                ConfigWrite(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_DNS_GENERAL, item->valuestring);
            } else if (!strcmp(item->string, "idleTime")) {
                ConfigWrite(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_IDLE_TIME, item->valuestring);
                int idleTime = atoi(item->valuestring);
                if (idleTime >= 60) {
                    sIdleTime = idleTime;
                }
            } else if (!strcmp(item->string, "reportInterval")) {
                int reportInterval = atoi(item->valuestring);
                if (reportInterval >= 60) {
                    ConfigWrite(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_REPORT_INTERVAL, item->valuestring);
                }
            } else if (!strcmp(item->string, "mlvpnServerIP")) {
                StrReplace(&sMlvpnServerIP, item->valuestring);
            } else if (!strcmp(item->string, "mlvpnIndex")) {
                sMlvpnIndex = (int)(item->valuedouble);
            } else if (!strcmp(item->string, "mlvpnEnabled")) {
                sMlvpnEnabled = (int)(item->valuedouble);
                mlvpnFound = 1;
            } else if (!strcmp(item->string, "schedules")) {
                // Should be an array of elements
                if (!cJSON_IsArray(item)) {
                    Logger(LOG_ERROR, "Expecting array");
                    continue;
                }
                Schedule_t* scheduleStart = NULL;
                Schedule_t* scheduleEnd = NULL;
                for (cJSON* item2 = item->child; item2; item2 = item2->next) {
                    cJSON* item3;
                    String config = NULL;
                    int timeOfDay = 0;
                    String daysOfWeek = NULL;
                    
                    Logger(LOG_DEBUG, "schedules: Array Item key '%s', type=%d", item2->string, item2->type);
                    if (!cJSON_IsObject(item2)) {
                        Logger(LOG_ERROR, "Expecting objects");
                        continue;
                    }
                    
                    item3 = cJSON_GetObjectItemCaseSensitive(item2, "config");
                    if (item3) {
                        Logger(LOG_DEBUG, "Found config");
                        StrReplace(&config, item3->valuestring);
                    }
                    
                    item3 = cJSON_GetObjectItemCaseSensitive(item2, "timeOfDay");
                    if (item3) {
                        Logger(LOG_DEBUG, "Found timeOfDay");
                        timeOfDay = (int)(item3->valuedouble);
                    }
                    
                    item3 = cJSON_GetObjectItemCaseSensitive(item2, "daysOfWeek");
                    if (item3) {
                        Logger(LOG_DEBUG, "Found daysOfWeek");
                        StrReplace(&daysOfWeek, item3->valuestring);
                    }
                    
                    // Create new schedule element
                    Schedule_t* sp = malloc(sizeof(*sp));
                    if (!sp) {
                        Logger(LOG_ERROR, "Could not allocate Schedule element");
                        continue;
                    }
                    memset(sp, 0, sizeof(*sp));
                    sp->config = config;
                    sp->category = ConfigGetBase(config);
                    sp->timeOfDay = timeOfDay;
                    sp->daysOfWeek = daysOfWeek;
                    sp->timeToExecute = ScheduleCalculateTime(sp);
                    
                    // Link in schedule
                    if (scheduleEnd) {
                        scheduleEnd->next = sp;
                    } else {
                        scheduleStart = sp;
                    }
                    scheduleEnd = sp;
                    Logger(LOG_DEBUG, "Schedule '%s' at %d", sp->config, sp->timeToExecute);
                }
                
                // Free old schedule
                for (Schedule_t* sp = sScheduleStart; sp; ) {
                    Schedule_t* sp2 = sp->next;
                    if (sp->config) {
                        free(sp->config);
                    }
                    if (sp->category) {
                        free(sp->category);
                    }
                    if (sp->daysOfWeek) {
                        free(sp->daysOfWeek);
                    }
                    free(sp);
                    sp = sp2;
                }
                
                // Set new schedule
#if DEBUG_SCHEDULE > 0    
                Logger(LOG_DEBUG, "PostResponse:New Schedule");
#endif
                sScheduleStart = scheduleStart;
                sScheduleEnd = scheduleEnd;
                
                // Determine if need to executed now 
                time_t ctime = time(NULL);
                for (Schedule_t* sp = sScheduleStart; sp; sp = sp->next) {
                    if (!sp->processed) {
                        // Determine min, max, closest below, closest above in same category
                        int minTime = 0;
                        int maxTime = 0;
                        int closestBelow = -1;
                        for (Schedule_t* sp2 = sScheduleStart; sp2; sp2 = sp2->next) {
                            if (strcmp(sp->category, sp2->category)) {
                                continue;
                            }
                            sp2->processed = TRUE;
                            if (!minTime || sp2->timeToExecute < minTime) {
                                minTime = sp2->timeToExecute;
                            }
                            if (!maxTime || sp2->timeToExecute > maxTime) {
                                maxTime = sp2->timeToExecute;
                            }
                            time_t diff = ctime - sp2->timeToExecute;
                            if (diff >= 0) {
                                if (closestBelow == -1 || diff < closestBelow) {
                                    closestBelow = diff;
                                }
                            }
                        }
                        
#if DEBUG_SCHEDULE > 0    
                        Logger(LOG_DEBUG, "PostResponse:minTime=%ld, maxTime=%ld, closestBelow=%ld", minTime, maxTime, closestBelow);
#endif

                        // Check if only one entry
                        if (minTime == maxTime) {
                            // Must be current entry, execute if past time
                            if (sp->timeToExecute < ctime) {
                                ExecuteConfig(sp->config);
                            }
                        } else {
                            // Multiple entries, determine which one to execute
                            for (Schedule_t* sp2 = sScheduleStart; sp2; sp2 = sp2->next) {
                                if (strcmp(sp->category, sp2->category)) {
                                    continue;
                                }
                                if (closestBelow != -1) {
                                    // Look for closest below
                                    time_t diff = ctime - sp2->timeToExecute;
                                    if (diff == closestBelow) {
                                        // This is the one to execute
                                        ExecuteConfig(sp2->config);
                                        break;
                                    }
                                } else {
                                    // Execute the max one
                                    if (sp2->timeToExecute == maxTime) {
                                        // This is the one to execute
                                        ExecuteConfig(sp2->config);
                                        break;
                                    }
                                }
                            }
                        }
                    }                    
                }
                
                // Check if needs to be scheduled for tomorrow
                for (Schedule_t* sp = sScheduleStart; sp; sp = sp->next) {
                    if (sp->timeToExecute < ctime) {
                        sp->timeToExecute += 24 * 60 * 60;
                    }
                }
             }
        }    
        if (sStatus) {
            free(sStatus);
            sStatus = NULL;
        }
        sStatus = GetStatus();
        //UpdateTemplate(NEDCONNECT_SCHOOL_TEMPLATE, NEDCONNECT_SCHOOL_BASE, &gNedSchoolFileNumber);
        //UpdateTemplate(NEDCONNECT_GENERAL_TEMPLATE, NEDCONNECT_GENERAL_BASE, &gNedGeneralFileNumber);
        //UpdateTemplate(NEDCONNECT_TEMPLATE, NEDCONNECT_BASE, &gNedConnectFileNumber);
        
        // Set Splash capture appropriately
        int schoolRequest = 0;
        if ((sNedMessage && strlen(sNedMessage)) || (sGroupMessage && strlen(sGroupMessage)) || (sDeviceMessage && strlen(sDeviceMessage))) {
            schoolRequest = 1;
        }
        
        int generalRequest = 0;
        Int64s percent;
        if (gGeneralBudget) {
            percent = (gTotalGeneralBytes * 100) / gGeneralBudget;
        } else {
            percent = 100;
        }
        if (schoolRequest || (percent >= 85)) {
            generalRequest = 1;
        }
        
        //SetSplash(schoolRequest, generalRequest);
        
        // Check if DNS needs to be updated
        DnsUpdate();

        // Send overage setting
        int overSchool = 0;
        if (gTotalSchoolBytes >= gSchoolBudget) {
            overSchool = 1;
        }
        int overGeneral = 0;
        if (gTotalGeneralBytes >= gGeneralBudget) {
            overGeneral = 1;
        }

        char command[100];
        sprintf(
            command, 
            "nednet2bqos2 -a%d -b%d -s%lld -g%lld -t%lld -h%lld",
            overSchool,
            overGeneral,
            schoolTargetKbps, 
            generalTargetKbps,
            schoolOBkbps,
            generalOBkbps
        );
        Logger(LOG_INFO, "Execute: '%s'", command);
        ExecuteCommand(command, FALSE);   

        // Check MLVPN
        /*if (mlvpnFound) {
            // Check if need to launch
            if (sMlvpnEnabled && !sMlvpnStarted) {
                // Update scripts
                TemplateDict_t vars[2];
                strcpy(vars[0].key, "^mlvpnIndex^");
                sprintf(vars[0].value, "%d", sMlvpnIndex); 
                strcpy(vars[1].key, "^mlvpnServerIP^");
                strcpy(vars[1].value, sMlvpnServerIP); 
                UpdateTemplateGeneral("/root/mlvpn/startTemplate.txt", "/root/mlvpn/start", vars, 2);
                ExecuteCommand("chmod 755 /root/mlvpn/start", FALSE);
                UpdateTemplateGeneral("/root/mlvpn/stopTemplate.txt", "/root/mlvpn/stop", vars, 2);
                ExecuteCommand("chmod 755 /root/mlvpn/stop", FALSE);
                ExecuteCommand("cd /root/mlvpn;/root/mlvpn/start &", FALSE);
                sMlvpnStarted = TRUE;
            } else if (!sMlvpnEnabled && sMlvpnStarted) {
                // Need to stop
                ExecuteCommand("/root/mlvpn/stop", FALSE);
                sMlvpnStarted = FALSE;
            }
        }*/
    } else if (!strcmp(response->messageId, "log")) {
        // No processing required
    } else {
        Logger(LOG_ERROR, "Invalid messageId '%s'", response->messageId);
    }
    
  errorExit:
    return ERR_OK;
}
    
//*******************************************************************
// ResetGeneralSplash
//-------------------------------------------------------------------
int ResetGeneralSplash() {
    int err = ERR_ERROR;
    String iptables = NULL;
    

    // Get get ip tables
    iptables = ReadTextFile("/tmp/usage.db");
    //ExecuteCommand("iptables -L -t nat|grep 'ACCEPT '|grep 192.168.9|sed 's/[\t ][\t ]*/ /g'|cut -d' ' -f4", &iptables);
    
    if (iptables) {  
        time_t ctime = time(NULL);
        
        for (String sp = strtok(iptables, "\n"); sp; sp = strtok(NULL, "\n")) {
            Logger(LOG_DEBUG, "ResetGeneralSplash: '%s'", sp);
            
            // Check if IP address
            /*if (strncmp(sp, "192.168.9", 9)) {
                Logger(LOG_DEBUG, "ResetGeneralSplash: No IP");
            } else {
                char buf[128];
                sprintf(buf, "iptables -t nat -D PREROUTING -p tcp -s %s -d 198.251.90.72 -j DNAT --to-destination 138.197.137.249", sp);
                Logger(LOG_DEBUG, "Executing '%s'", buf);
                ExecuteCommand(buf, NULL);
                
                sprintf(buf, "iptables -t nat -D PREROUTING -s %s -j ACCEPT", sp);
                Logger(LOG_DEBUG, "Executing '%s'", buf);
                ExecuteCommand(buf, NULL);
            }*/

            // Check for comment
            if (strncmp(sp, "#", 1) == 0) {
                continue;
            }
            
            // Parse line
            String* words = NULL;
            words = ParseWords(sp, ',');
            if (words) {
                int i;
                for (i = 0; words[i]; i++) {
                    ;
                }
                if (i == 8) {
                    Logger(LOG_DEBUG, "ResetGeneralSplash: Have entry");
                    // Check for general
                    if (strcmp(words[2], "br-guest") == 0) {
                        time_t idleTime = ctime - ConvertDate(words[7]) - 21600;
                        Logger(LOG_DEBUG, "ResetGeneralSplash: Idle time=%ld", (long)idleTime);
                        if (idleTime > sIdleTime) {
                            char buf[128];
                            //sprintf(buf, "iptables -t nat -D PREROUTING -p tcp -s %s -d 198.251.90.72 -j DNAT --to-destination 138.197.137.249", words[1]);
                            //Logger(LOG_DEBUG, "Executing '%s'", buf);
                            //ExecuteCommand(buf, NULL);
                            
                            sprintf(buf, "iptables -t nat -D PREROUTING -s %s -j ACCEPT", words[1]);
                            Logger(LOG_DEBUG, "Executing '%s'", buf);
                            ExecuteCommand(buf, NULL);
                        }
                    } else {
                        Logger(LOG_DEBUG, "ResetGeneralSplash: Not general");
                    }
                } else {
                    Logger(LOG_DEBUG, "ResetGeneralSplash: Invalid number of words=%d", i);
                }
                ParseWordsFree(words);
            }
        }
    }
    
    err = ERR_OK;
    if (iptables) {
        free(iptables);
    }    
    return err;
}

//*******************************************************************
// GetUciConfig
//-------------------------------------------------------------------
int GetUciConfig() {
    String rp = NULL;
    
    ExecuteCommand("uci get nednet.traffic.dsl", &rp);
    if (!rp || strlen(rp) < 1 || !strncmp(rp, "uci:", 4)) {
        // Not proper config, use default
        sStatsDslRxByteFile = STATS_DSL_RX_BYTES;
        sStatsDslTxByteFile = STATS_DSL_TX_BYTES;
    } else {
        // Form file path, first remove any trailing whitespace
        String sp = strtok(rp, " \r\n");
        sStatsDslRxByteFile = StrConcat(sStatsDslRxByteFile, STATS_PREFIX);
        sStatsDslRxByteFile = StrConcat(sStatsDslRxByteFile, sp);
        sStatsDslRxByteFile = StrConcat(sStatsDslRxByteFile, STATS_RX_SUFFIX);
        
        // Check if file exists
        if (access(sStatsDslRxByteFile, F_OK ) != 0) {
            Logger(LOG_ERROR, "Cannot access sStatsDslRxByteFile: '%s'", sStatsDslRxByteFile);
        }
        
        // Form file path
        sStatsDslTxByteFile = StrConcat(sStatsDslTxByteFile, STATS_PREFIX);
        sStatsDslTxByteFile = StrConcat(sStatsDslTxByteFile, sp);
        sStatsDslTxByteFile = StrConcat(sStatsDslTxByteFile, STATS_TX_SUFFIX);
        
        // Check if file exists
        if (access(sStatsDslTxByteFile, F_OK ) != 0) {
            Logger(LOG_ERROR, "Cannot access sStatsDslTxByteFile: '%s'", sStatsDslTxByteFile);
        }        
    }
    FREE(rp);
        
    ExecuteCommand("uci get nednet.traffic.lte", &rp);
    if (!rp || strlen(rp) < 1 || !strncmp(rp, "uci:", 4)) {
        // Not proper config, use default
        sStatsLteRxByteFile = STATS_LTE_RX_BYTES;
        sStatsLteTxByteFile = STATS_LTE_TX_BYTES;
    } else {
        // Form file path, first remove any trailing whitespace
        String sp = strtok(rp, " \r\n");
        sStatsLteRxByteFile = StrConcat(sStatsLteRxByteFile, STATS_PREFIX);
        sStatsLteRxByteFile = StrConcat(sStatsLteRxByteFile, sp);
        sStatsLteRxByteFile = StrConcat(sStatsLteRxByteFile, STATS_RX_SUFFIX);
        
        // Check if file exists
        if (access(sStatsLteRxByteFile, F_OK ) != 0) {
            Logger(LOG_ERROR, "Cannot access sStatsLteRxByteFile: '%s'", sStatsLteRxByteFile);
        }
        
        // Form file path
        sStatsLteTxByteFile = StrConcat(sStatsLteTxByteFile, STATS_PREFIX);
        sStatsLteTxByteFile = StrConcat(sStatsLteTxByteFile, sp);
        sStatsLteTxByteFile = StrConcat(sStatsLteTxByteFile, STATS_TX_SUFFIX);
        
        // Check if file exists
        if (access(sStatsLteTxByteFile, F_OK ) != 0) {
            Logger(LOG_ERROR, "Cannot access sStatsLteTxByteFile: '%s'", sStatsLteTxByteFile);
        }        
    }
    FREE(rp);
        
    ExecuteCommand("uci get nednet.traffic.dns", &rp);
    if (rp && strlen(rp) > 0 & strncmp(rp, "uci:", 4)) {
        sDnsEnable = atoi(rp);
    }
    FREE(rp);
    
    ExecuteCommand("uci get nednet.traffic.splash", &rp);
    if (rp && strlen(rp) > 0 & strncmp(rp, "uci:", 4)) {
        sSplashEnable = atoi(rp);
    }
    FREE(rp);
    
    return 0;
}

//*******************************************************************
// main
//-------------------------------------------------------------------
int main(int argc, char **argv) {
    // Add to boot sequence
    AddToBoot(gName);

    if (argc > 1) {
        MacRead();
        
        printf("     Version: '%s'\n", gVersion);
        printf("         MAC: '%s'\n", gMAC);
        return 0;
    }
    
    gPostResponse = &PostResponse;
    MacRead();

    // Check for config file
    String api = NULL;
    String apiBase = ConfigGetApi();
    if (apiBase) {
        api = apiBase;
    } else {
        api = StrDuplicate(API_BASE);
    }
    api = StrConcat(api, API_FILE);
    ApiInit(api);
    free(api);
    
    LoggerInit(gName);
    Logger(LOG_INFO, "Version: '%s'", gVersion);
    
    //rxBytes();
    //txBytes();
    //gettimeofday(&gLastStatsTime, NULL);
    
    Logger(LOG_INFO, "MAC: '%s'", gMAC);
    
    if (!strcmp(gMAC, "")) {
        Logger(LOG_ERROR, "No MAC found.");
        goto errorExit;
    }
    
    // Extract SSID5
    int l = strlen(gMAC);
    if (l >= 7) {
        gSSID3[0] = gMAC[l - 7];
        gSSID3[1] = gMAC[l - 5];
        gSSID3[2] = gMAC[l - 4];
        gSSID3[3] = gMAC[l - 2];
        gSSID3[4] = gMAC[l - 1];
        gSSID3[5] = 0;
    } else {
        gSSID3[0] = 0;
    }
    
    StrReplace(&gOrgName, "");
    StrReplace(&sSchoolUrl, "");
    StrReplace(&sPhoneNumber, "");

    GetUciConfig();
    
    if (sDnsEnable) {
        // Set up iptables DNS
        SetIpTablesNat(1, "-i br-lan -s 0/0 -p udp --dport 53 -j DNAT --to 192.168.8.1");
        SetIpTablesNat(2, "-i br-guest -s 0/0 -p udp --dport 53 -j DNAT --to 192.168.9.1");
        
        // Blocking page redirection
        SetIpTablesNat(3, "-s 0/0 -p tcp -d 198.251.90.72 -j DNAT --to-destination 138.197.137.249");
    }
    
    // Determine if should be off or on
    if (sSplashEnable) {
        String noSplash = ConfigRead(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_NO_SPLASH);
        if (!noSplash) {
            // Set up splash pages
            SetIpTablesNat(4, "-i br-lan -s 0/0 -p tcp --dport 80 -j DNAT --to 192.168.8.1:81");
            SetIpTablesNat(5, "-i br-guest -s 0/0 -p tcp --dport 80 -j DNAT --to 192.168.9.1:82");
        }
    }
    
    //SetSplash(1, 1);
    
    Logger(LOG_INFO, "Entering main loop");
    sStatus = GetStatus();
    MessageTrafficStats();
    //UpdateTemplate(NEDCONNECT_SCHOOL_TEMPLATE, NEDCONNECT_SCHOOL_BASE, &gNedSchoolFileNumber);
    //UpdateTemplate(NEDCONNECT_GENERAL_TEMPLATE, NEDCONNECT_GENERAL_BASE, &gNedGeneralFileNumber);
    //UpdateTemplate(NEDCONNECT_TEMPLATE, NEDCONNECT_BASE, &gNedConnectFileNumber);
    
    // Check if DNS needs to be updated
    if (sDnsEnable) {
        DnsUpdate();
    }
    
    String sp = ConfigRead(NEDCONNECT_TRAFFIC_CONFIG, CONFIG_IDLE_TIME);
    if (sp) {
        int idleTime = atoi(sp);
        if (idleTime >= 60) {
            sIdleTime = idleTime;
        }
    }
    
    time_t nextResetTime = time(NULL) + 60;
    
    for ( ; ; ) {
        MessageTrafficStats();
        sleep(DEFAULT_SAMPLING_INTERVAL);
        
        LoggerCheckRotate();
        
        // Check if no server contact
        if (sNoResponse >= NO_RESPONSE_COUNT) {
            Logger(LOG_DEBUG, "No server response, sending command to throttle.");
            ExecuteCommand("nednet2bqos2 -a1 -b1", NULL);
        }
        
        time_t ctime = time(NULL);
        
        /*if (ctime >= nextResetTime) {
            ResetGeneralSplash();
            nextResetTime = ctime + 60;
        }*/
        
        // Check if schedule has past
        for (Schedule_t* sp = sScheduleStart; sp; sp = sp->next) {
            if (sp->timeToExecute < ctime) {
                sp->timeToExecute = ScheduleCalculateNextTime(sp);
                ExecuteConfig(sp->config);
            }
        }
    }
    
  errorExit:
	Logger(LOG_INFO, "Done");
    ApiQuit();
	return 0;
}
