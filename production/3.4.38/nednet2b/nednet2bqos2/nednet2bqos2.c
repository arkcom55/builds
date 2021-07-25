//*******************************************************************
// File:  nednet2bqos2.c
//-------------------------------------------------------------------
// Date:  2020-07-18
// Copyright (c) 2020 Neducation Management Inc.
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

#include "cJSON.h"
#include "nednet2bcommon.h"

static int sEnabled = FALSE;

String gName = "nednet2bqos2";
String gVersion = "nednet2bqos2_3.2-38";

#define API_BASE "https://dnsapi.neducation.ca/api/"
#define API_FILE "nedNet2b.php"

#ifdef TEST
int gTest = 1;
#define GLQOS_CONFIG  "glqos"
#else
int gTest = 0;
#define GLQOS_CONFIG  "/etc/config/glqos"
#endif

#define WRTBWMON_FILE    "/tmp/usage.db"
#define WRTBWMON_SETUP   "wrtbwmon setup /tmp/usage.db"
#define WRTBWMON_COMMAND "wrtbwmon update /tmp/usage.db"

#define BASE_CONFIG_SCHOOL              "/root/qosSchool.json"
#define BASE_CONFIG_SCHOOL_OVERBUDGET   "/root/qosSchoolOverbudget.json"
#define CONFIG_SCHOOL                   "/tmp/qosSchool.json"
#define CONFIG_SCHOOL_OVERBUDGET        "/tmp/qosSchoolOverbudget.json"

#define BASE_CONFIG_GENERAL             "/root/qosGeneral.json"
#define BASE_CONFIG_GENERAL_OVERBUDGET  "/root/qosGeneralOverbudget.json"
#define CONFIG_GENERAL                  "/tmp/qosGeneral.json"
#define CONFIG_GENERAL_OVERBUDGET       "/tmp/qosGeneralOverbudget.json"

#define FREE(rp) {if (rp) {free(rp); rp = NULL;}}

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

typedef struct Bucket_s {
    time_t   lastTimestamp; 
    
    // From usage
    char     mac[20];
    char     ip[20];
    char     iface[40];
    Int64s   in;
    Int64s   out;
    Int64s   total;
    Int64s   lastTotal;
    time_t   lastActiveTime;
    
    // Configuration
    QosConfig_t* qosConfig;

    // Current values
    long     initialTargetRate;
    long     currentSize;
    long     targetRate;
    int      timeToTarget;
    time_t   setTime;
    
    // Control
    int      throttled;         // Throttled flag
    long     currentTargetRate; // Rate to set in glqos
    long     emptyRate;
    
    // Link for bucket list
    struct   Bucket_s* next;
} Bucket_t;

typedef struct Qos_s {
    char     mac[20];
    int      throttled;
    long     rate;
    QosConfig_t* qosConfig;
    struct   Qos_s* next;
} Qos_t;

//*******************************************************************
// ConvertDate
//-------------------------------------------------------------------
time_t ConvertDate(String date) {
    struct tm tinfo;
    char buf[20];
    String sp;
    String sp2;
    
    //Logger(LOG_DEBUG, "nednet2bqos2: ConvertDate '%s'", date);
    
    // Day
    sp = date;
    sp2 = buf;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2 = 0;
    tinfo.tm_mday = atoi(buf);
    
    // Month
    sp = date + 3;
    sp2 = buf;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2 = 0;
    tinfo.tm_mon = atoi(buf) - 1;
    
    // Year
    sp = date + 6;
    sp2 = buf;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2 = 0;
    tinfo.tm_year = atoi(buf) - 1900;
    
    // Hour
    sp = date + 11;
    sp2 = buf;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2 = 0;
    tinfo.tm_hour = atoi(buf);
    
    // Minute
    sp = date + 14;
    sp2 = buf;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2 = 0;
    tinfo.tm_min = atoi(buf);
    
    // Second
    sp = date + 17;
    sp2 = buf;
    *sp2++ = *sp++;
    *sp2++ = *sp++;
    *sp2 = 0;
    tinfo.tm_sec = atoi(buf);
    
    return mktime(&tinfo);
}

//*******************************************************************
// ParseWrtbwmon
//-------------------------------------------------------------------
Wrtbwmon_t* ParseWrtbwmon(String bp) {
    Wrtbwmon_t* p = NULL;
    String sp;
    
    //Logger(LOG_DEBUG, "nednet2bqos2: Parsing '%s'", bp);
    
    // Skip comment lines
    if (bp[0] == '#') {
        goto errorExit;
    }
    
    p = malloc(sizeof(*p));
    if (!p) {
        Logger(LOG_ERROR, "nednet2bqos2: Could not allocate usage structure");
        goto errorExit;
    }
    memset(p, 0, sizeof(*p));
    
    sp = strtok(bp, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos2: Could not parse mac");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->mac, sp, sizeof(p->mac) - 1);
    
    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse ip");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->ip, sp, sizeof(p->ip) - 1);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse iface");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->iface, sp, sizeof(p->iface) - 1);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse in");
        free(p);
        p = NULL;
        goto errorExit;
    }
    p->in = atoll(sp);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse out");
        free(p);
        p = NULL;
        goto errorExit;
    }
    p->out = atoll(sp);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse total");
        free(p);
        p = NULL;
        goto errorExit;
    }
    p->total = atoll(sp);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse first_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->firstDate, sp, sizeof(p->firstDate) - 1);

    sp = strtok(NULL, ",\n");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse last_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
    strncpy(p->lastDate, sp, sizeof(p->lastDate) - 1);

    // Convert dates to timestamp values
    p->firstTimestamp = ConvertDate(p->firstDate);
    if (p->firstTimestamp == (time_t)(-1)) {
        Logger(LOG_ERROR, "nednet2bqos: Could not convert first_date");
        free(p);
        p = NULL;
        goto errorExit;
    }
    
    p->lastTimestamp = ConvertDate(p->lastDate);
    if (p->lastTimestamp == (time_t)(-1)) {
        Logger(LOG_ERROR, "nednet2bqos: Could not convert last_date");
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
    
    ExecuteCommand(WRTBWMON_COMMAND, &response);
    if (response) {
        Logger(LOG_DEBUG, "GetUsage update response: '%s'", response);
        free(response);
    }
    fp = fopen(WRTBWMON_FILE, "r");
    if (!fp) {
        Logger(LOG_ERROR, "nednet2bqos2: Could not open usage db");
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
// Message: QoSParameters
//-------------------------------------------------------------------
/*int MessageQoSParameters() {
    int             status = ERR_ERROR;
    Message_t*      mp = NULL;
    Parameters_t    parameters;
    
    Logger(LOG_DEBUG, "Message QoSParameters");
    
    ParametersInit(&parameters);
    mp = MessageConstruct("qosParameters", &parameters);
    if (!mp) {
        goto errorExit;
    }
    Logger(LOG_DEBUG, "Sending QoSParameters");
    if (MessagePost(mp, TRUE)) {
        goto errorExit;
    }
    
    status = ERR_OK;
    
  errorExit:
    ParametersFree(&parameters);
    if (mp) {
        free(mp);
    }
    return status;
}*/

//*******************************************************************
// PostResponse
//-------------------------------------------------------------------
/*static int PostResponse(Response_t* response) {
    if (!strcmp(response->messageId, "qosParameters")) {
        Logger(LOG_DEBUG, "Received qosParameters response");
        long schoolTargetKbps = 0;
        long generalTargetKbps = 0;
        for (cJSON* item = response->data; item; item = item->next) {
            Logger(LOG_DEBUG, "Item key '%s', type=%d", item->string, item->type);
            if (!strcmp(item->string, "schoolTargetKbps")) {
                schoolTargetKbps = (long)(item->valuedouble);
            }
            if (!strcmp(item->string, "generalTargetKbps")) {
                generalTargetKbps = (long)(item->valuedouble);
            }
        }
        if (schoolTargetKbps) {
            QosConfig_t qconfig;
            InitQosConfig(&qconfig);
            qconfig.targetRate = schoolTargetKbps * 1000;
            WriteQosConfig(CONFIG_SCHOOL, &qconfig);
        }
        if (generalTargetKbps) {
            QosConfig_t qconfig;
            InitQosConfig(&qconfig);
            qconfig.targetRate = generalTargetKbps * 1000;
            WriteQosConfig(CONFIG_GENERAL, &qconfig);
        }
    } else if (!strcmp(response->messageId, "log")) {
        // No processing required
    } else {
        Logger(LOG_ERROR, "Invalid messageId '%s'", response->messageId);
    }
    
  errorExit:
    return ERR_OK;
}*/
    
//*******************************************************************
// GetUciConfig
//-------------------------------------------------------------------
int GetUciConfig() {
    String rp = NULL;
    
    ExecuteCommand("uci get nednet.qos.enabled", &rp);
    if (rp && strlen(rp) > 0 & strncmp(rp, "uci:", 4)) {
        sEnabled = atoi(rp);
    }
    FREE(rp);
    
    return 0;
}

//*******************************************************************
// main
//-------------------------------------------------------------------
int main(int argc, char **argv) {
    int rate;
    Bucket_t* bstart = NULL;
    Bucket_t* bend = NULL;
    
    String glqosOld = NULL;
    
    // Add to boot sequence
    AddToBoot(gName);
    
    if (argc > 1) {
        char c;
        int school = FALSE;
        long schoolValue;
        int schoolOB = FALSE;
        long schoolOBValue;
        int general = FALSE;
        long generalValue;
        int generalOB = FALSE;
        long generalOBValue;
        int overageSchool = FALSE;
        int overageSchoolOnOff;
        int overageGeneral = FALSE;
        int overageGeneralOnOff;
        MacRead();
        
        while ((c = getopt(argc, argv, "a:b:cs:t:g:h:x")) != -1) {
            switch (c) {
                case 'c':
                    printf("Version: '%s'\n", gVersion);
                    printf("    MAC: '%s'\n", gMAC);
                    break;
                case 's':
                    school = TRUE;
                    schoolValue = atol(optarg);
                    break;
                case 't':
                    schoolOB = TRUE;
                    schoolOBValue = atol(optarg);
                    break;
                case 'g':
                    general = TRUE;
                    generalValue = atol(optarg);
                    break;
                case 'h':
                    generalOB = TRUE;
                    generalOBValue = atol(optarg);
                    break;
                case 'a':
                    overageSchool = TRUE;
                    overageSchoolOnOff = atoi(optarg);
                    break;
                case 'b':
                    overageGeneral = TRUE;
                    overageGeneralOnOff = atoi(optarg);
                    break;
                default:
                    break;
            }
        }
        
        QosConfig_t qconfig;
        InitQosConfig(&qconfig);
        ReadQosConfig(BASE_CONFIG_SCHOOL, &qconfig);
        if (school) {
            if (schoolValue) {
                qconfig.targetRate = schoolValue;
            } else if (!qconfig.targetRate) {
                qconfig.targetRate = DEFAULT_SCHOOL_TARGET_RATE;
            }
        }
        if (overageSchool) {
            if (overageSchoolOnOff) {
                qconfig.overBudget = TRUE;
            } else {
                qconfig.overBudget = FALSE;
            }
        } 
        WriteQosConfig(BASE_CONFIG_SCHOOL, &qconfig);
        WriteQosConfig(CONFIG_SCHOOL, &qconfig);

        InitQosConfig(&qconfig);
        ReadQosConfig(BASE_CONFIG_SCHOOL_OVERBUDGET, &qconfig);
        if (schoolOB) {
            if (schoolOBValue) {
                qconfig.targetRate = schoolOBValue;
            } else if (!qconfig.targetRate) {
                qconfig.targetRate = DEFAULT_OVERBUDGET_SCHOOL_TARGET_RATE;
            }
        }
        WriteQosConfig(BASE_CONFIG_SCHOOL_OVERBUDGET, &qconfig);
        WriteQosConfig(CONFIG_SCHOOL_OVERBUDGET, &qconfig);

        InitQosConfig(&qconfig);
        ReadQosConfig(BASE_CONFIG_GENERAL, &qconfig);
        if (general) {
            if (generalValue) {
                qconfig.targetRate = generalValue;
            } else if (!qconfig.targetRate) {
                qconfig.targetRate = DEFAULT_GENERAL_TARGET_RATE;
            }
        }
        if (overageGeneral) {
            if (overageGeneralOnOff) {
                qconfig.overBudget = TRUE;
            } else {
                qconfig.overBudget = FALSE;
            }
        } 
        WriteQosConfig(BASE_CONFIG_GENERAL, &qconfig);
        WriteQosConfig(CONFIG_GENERAL, &qconfig);
        
        InitQosConfig(&qconfig);
        ReadQosConfig(BASE_CONFIG_GENERAL_OVERBUDGET, &qconfig);
        if (generalOB) {
            if (generalOBValue) {
                qconfig.targetRate = generalOBValue;
            } else if (!qconfig.targetRate) {
                qconfig.targetRate = DEFAULT_OVERBUDGET_GENERAL_TARGET_RATE;
            }
        }
        WriteQosConfig(BASE_CONFIG_GENERAL_OVERBUDGET, &qconfig);
        WriteQosConfig(CONFIG_GENERAL_OVERBUDGET, &qconfig);
        
        return 0;
    }

    GetUciConfig();
    if (!sEnabled) {
        return 0;
    }
    
    // Delay and the read MAC
    sleep(10);
    MacRead();
    
    //gPostResponse = &PostResponse;
    
    // Check for config file
    String api = NULL;
    String apiBase = ConfigRead();
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
    Logger(LOG_INFO, "MAC: '%s'", gMAC);
    
    if (!strcmp(gMAC, "")) {
        Logger(LOG_ERROR, "nednet2bqos2: No MAC found.");
        goto errorExit;
    }
    
    // Create default config files
    if (!gTest) {
        QosConfig_t qconfig;
        
        InitQosConfig(&qconfig);
        ReadQosConfig(BASE_CONFIG_SCHOOL, &qconfig);
        WriteQosConfig(CONFIG_SCHOOL, &qconfig);
        
        InitQosConfigOverbudget(&qconfig);
        ReadQosConfig(BASE_CONFIG_SCHOOL_OVERBUDGET, &qconfig);
        WriteQosConfig(CONFIG_SCHOOL_OVERBUDGET, &qconfig);
        
        InitQosConfig(&qconfig);
        ReadQosConfig(BASE_CONFIG_GENERAL, &qconfig);
        WriteQosConfig(CONFIG_GENERAL, &qconfig);
        
        InitQosConfigOverbudget(&qconfig);
        ReadQosConfig(BASE_CONFIG_GENERAL_OVERBUDGET, &qconfig);
        WriteQosConfig(CONFIG_GENERAL_OVERBUDGET, &qconfig);
    }
    
    if (!gTest) {
        ExecuteCommand("uci set glconfig.switch.enable=\"1\"", NULL);
        ExecuteCommand("uci commit glconfig", NULL);
        
        Logger(LOG_DEBUG, "nednet2bqos2: Initializing wrtbwmon...");
        ExecuteCommand("rm /tmp/usage.db", NULL);
        ExecuteCommand(WRTBWMON_SETUP, NULL);
    }
    
    //MessageQoSParameters();
    
    Logger(LOG_INFO, "nednet2bqos2: Processing...");
    for ( ; ; ) {
        
        // Read in parameters for school
        QosConfig_t qconfigSchool;
        InitQosConfig(&qconfigSchool);
        ReadQosConfig(CONFIG_SCHOOL, &qconfigSchool);
        String version = "--";
        if (qconfigSchool.overBudget) {
            InitQosConfigOverbudget(&qconfigSchool);
            ReadQosConfig(CONFIG_SCHOOL_OVERBUDGET, &qconfigSchool);
            version = "OB";
        }
        if (qconfigSchool.targetRate > qconfigSchool.initialTargetRate) {
            qconfigSchool.initialTargetRate = qconfigSchool.targetRate;
        }
        Logger(LOG_DEBUG, 
            "nednet2bqos2:  School(%s): %ld/%ld/%.1f/%d/%.1f %ld/%ld", 
            version,
            qconfigSchool.initialTargetRate, 
            qconfigSchool.targetRate, 
            qconfigSchool.targetRateMultiplier, 
            qconfigSchool.targetRateDecayTime, 
            qconfigSchool.drainRateMultiplier,
            qconfigSchool.bucketSize, 
            qconfigSchool.restartSize
        );
        
        // Read in parameters for general
        QosConfig_t qconfigGeneral;
        InitQosConfig(&qconfigGeneral);
        ReadQosConfig(CONFIG_GENERAL, &qconfigGeneral);  
        version = "--";        
        if (qconfigGeneral.overBudget) {
            InitQosConfigOverbudget(&qconfigGeneral);
            ReadQosConfig(CONFIG_GENERAL_OVERBUDGET, &qconfigGeneral);
            version = "OB";
        }
        if (qconfigGeneral.targetRate > qconfigGeneral.initialTargetRate) {
            qconfigGeneral.initialTargetRate = qconfigGeneral.targetRate;
        }
        Logger(LOG_DEBUG, 
            "nednet2bqos2: General(%s): %ld/%ld/%.1f/%d/%.1f %ld/%ld", 
            version,
            qconfigGeneral.initialTargetRate, 
            qconfigGeneral.targetRate, 
            qconfigGeneral.targetRateMultiplier, 
            qconfigGeneral.targetRateDecayTime, 
            qconfigGeneral.drainRateMultiplier,
            qconfigGeneral.bucketSize, 
            qconfigGeneral.restartSize
        );
            
        // Get usage
        Wrtbwmon_t* usage = GetUsage();
        
        // Check if all zeros
        int nonZero = FALSE;
        for (Wrtbwmon_t* p = usage; p; p = p->next) {
            if (p->in || p->out) {
                nonZero = TRUE;
                break;
            }
        }
        if (!nonZero) {
            // All zeros reinitialize wrtbwmon
            Logger(LOG_DEBUG, "nednet2bqos2: Reinitializing wrtbwmon...");
            ExecuteCommand(WRTBWMON_SETUP, NULL);
            sleep(DEFAULT_SAMPLING_PERIOD);
            continue;
        }
        
        // Process usage data
        time_t ctime = time(NULL);
        for (Wrtbwmon_t* p = usage; p; p = p->next) {

            // Determine if school or general
            QosConfig_t* qp;
            if (strstr(p->ip, "192.168.8")) {
                qp = &qconfigSchool;
            } else {
                qp = &qconfigGeneral;
            }

            // Look for bucket entry
            Bucket_t* bp;
            for (bp = bstart; bp; bp = bp->next) {
                if (!strcmp(bp->mac, p->mac) && !strcmp(bp->ip, p->ip)) {
                    break;
                }
            }
            
            // Check if bucket found
            if (!bp) {
                // Check if no bucket then skip
                if (!qp->bucketSize) {
                    continue;
                }

                // New bucket, create bucket
                bp = malloc(sizeof(*bp));
                if (!bp) {
                    Logger(LOG_ERROR, "nednet2bqos2: Cannot allocate bucket.");
                    continue;
                }
                memset(bp, 0, sizeof(*bp));
                bp->lastTimestamp = ctime;
                bp->qosConfig = qp;
                
                // Fill in bucket with initial values
                strcpy(bp->mac, p->mac);
                strcpy(bp->ip, p->ip);
                strcpy(bp->iface, p->iface);
                bp->in = p->in;
                bp->out = p->out;
                bp->total = p->total;
                bp->lastActiveTime = p->lastTimestamp;
                
                // Default initial state
                bp->currentTargetRate = qp->initialTargetRate;
                bp->targetRate = qp->initialTargetRate;
                bp->emptyRate = qp->targetRate;
                
                // Link into bucket list
                if (bend) {
                    bend->next = bp;
                } else {
                    bstart = bp;
                }
                bend = bp;
                
                Logger(LOG_DEBUG, "nednet2bqos2: New Bucket %s/%s", bp->mac, bp->ip);
             } else {
                // Existing bucket
                bp->lastActiveTime = p->lastTimestamp;
                
                // Check if no bucket (then full rate)
                if (!qp->bucketSize) {
                    bp->throttled = FALSE;
                    bp->currentTargetRate = qp->initialTargetRate;
                    continue;
                }
                
                // Check if change since last processing time
                long used = (long)(p->total - bp->total);
                time_t dTime = ctime - bp->lastTimestamp;
                if (dTime) {
                    // Process change since last time
                    
                    // Determine new size of bucket
                    long deltaTarget = bp->emptyRate / 8 * dTime * qp->drainRateMultiplier;
                    long newLength = (used - deltaTarget) + bp->currentSize;
                    if (newLength <= 0) {
                        newLength = 0;
                    } 
                    bp->emptyRate = qp->targetRate;
                    
                    // Check if bucket indicates do not throttle
                    if (newLength <= qp->restartSize) {
                        // Length below restart size, unthrottle
                        bp->throttled = FALSE;
                        bp->currentTargetRate = qp->initialTargetRate;
                    } else if (newLength >= qp->bucketSize || (bp->throttled && newLength >= qp->restartSize)) {
                        // Set throttling
                        
                        // Limit bucket to maximum size
                        if (newLength > qp->bucketSize) {
                            newLength = qp->bucketSize;
                        }
                        
                        // Check if already throttled
                        if (!bp->throttled) {
                            // New throttle, set up
                            if (qp->targetRateMultiplier > 1) {
                                bp->initialTargetRate = (Int64s)(qp->targetRate * qp->targetRateMultiplier);
                            } else {
                                bp->initialTargetRate = qp->targetRate;
                            }
                            bp->targetRate = qp->targetRate;
                            bp->currentTargetRate = bp->initialTargetRate;
                            bp->timeToTarget = qp->targetRateDecayTime;
                            bp->setTime = ctime;
                        } else {
                            // Existing throttle, adjust rate is necessary
                            bp->targetRate = qp->targetRate;
                            if (bp->currentTargetRate > bp->targetRate) {
                                // Current rate needs to be adjusted
                                
                                // Check if being done over time
                                if (qp->targetRateDecayTime) {
                                    // Over time, decrease rate
                                    bp->currentTargetRate = bp->targetRate + (Int64s)((1.0 - ((float)(ctime - bp->setTime)) / (qp->targetRateDecayTime * 1.0)) * (bp->initialTargetRate - bp->targetRate));
                                } else {
                                    // Not over time, set end point
                                    bp->currentTargetRate = bp->targetRate;
                                }
                            }                             
                        }

                        // Current rate cannot be less than target rate
                        if (bp->currentTargetRate < bp->targetRate) {
                            bp->currentTargetRate = bp->targetRate;
                        } 

                        // Set throttle flag
                        bp->throttled = TRUE;
                    }
                    bp->currentSize = newLength;
                }
                bp->lastTimestamp = ctime;
                
                // Record usage info
                bp->in = p->in;
                bp->out = p->out;
                bp->lastTotal = bp->total;
                bp->total = p->total;
            }
        }
        
        // Use last bucket for the same mac
        Qos_t* qstart = NULL;
        Qos_t* qend = NULL;
        for (Bucket_t* bp = bstart; bp; bp = bp->next) {
            Logger(LOG_DEBUG, "nednet2bqos2: Bucket %s/%s, size = %ld, %s %ld kbps, time = %ld", bp->mac, bp->ip, bp->currentSize, bp->throttled ? "Throttled" : "Not Throttled", bp->currentTargetRate / 1000, bp->lastActiveTime);
            
            // Check for second bucket with same mac
            Bucket_t* bp2;
            for (bp2 = bp->next; bp2; bp2 = bp2->next) {
                if (!strcmp(bp->mac, bp2->mac)) {
                    break;
                }
            }
            
            // Check if already processed
            Qos_t* qp;
            for (qp = qstart; qp; qp = qp->next) {
                if (!strcmp(qp->mac, bp->mac)) {
                    break;
                }
            }
            if (qp) {
                continue;
            }
            
            // Determine which bucket to use (if two)
            Bucket_t* bp3 = bp;
            if (bp2) {
                if (bp2->lastActiveTime > bp->lastActiveTime) {
                    bp3 = bp2;
                } else if (bp2->lastActiveTime == bp->lastActiveTime) {
                    if (bp2->lastTotal < bp2->total) {
                        bp3 = bp2;
                    }
                }
            }
            
            // Add QOS entry
            qp = malloc(sizeof(*qp));
            if (!qp) {
                Logger(LOG_ERROR, "nednet2bqos2: Cannot allocate qos entry.");
                continue;
            }
            memset(qp, 0, sizeof(*qp));
            strcpy(qp->mac, bp3->mac);
            qp->throttled = bp3->throttled;
            qp->rate = bp3->currentTargetRate;
            qp->qosConfig = bp3->qosConfig;
            if (qend) {
                qend->next = qp;
            } else {
                qstart = qp;
            }
            qend = qp;
        }
        
        // Create config
        String glqos = NULL;
        Qos_t* qp;
        for (qp = qstart; qp; qp = qp->next) {
            char buf[40];            
            char buf2[80]; 
            long rate = 0;
            
            if (qp->throttled && qp->qosConfig->bucketSize) {
                Logger(LOG_DEBUG, "nednet2bqos2: MAC %s throttle=%ld kbps", qp->mac, (qp->rate)/1000);
                rate = qp->rate;
            } else if (qp->qosConfig->initialTargetRate) {
                Logger(LOG_DEBUG, "nednet2bqos2: MAC %s initial throttle=%ld kbps", qp->mac, (qp->qosConfig->initialTargetRate)/1000);
                rate = qp->qosConfig->initialTargetRate;
            }
            
            if (rate) {
                buf[0] = *(qp->mac);
                buf[1] = *(qp->mac + 1);
                buf[2] = *(qp->mac + 3);
                buf[3] = *(qp->mac + 4);
                buf[4] = *(qp->mac + 6);
                buf[5] = *(qp->mac + 7);
                buf[6] = *(qp->mac + 9);
                buf[7] = *(qp->mac + 10);
                buf[8] = *(qp->mac + 12);
                buf[9] = *(qp->mac + 13);
                buf[10] = *(qp->mac + 15);
                buf[11] = *(qp->mac + 16);
                buf[12] = 0;
                sprintf(buf2, "config qos '%s'\n", buf);
                glqos = StrConcat(glqos, buf2);
                sprintf(buf2, "    option mac '%s'\n", qp->mac);
                glqos = StrConcat(glqos, buf2);
                sprintf(buf2, "    option upload '%ld'\n", rate/8000);
                glqos = StrConcat(glqos, buf2);
                sprintf(buf2, "    option download '%ld'\n\n", rate/8000);
                glqos = StrConcat(glqos, buf2);
            }
        }
        
        if ((glqos && !glqosOld) || (!glqos && glqosOld) || (glqos && glqosOld && strcmp(glqos, glqosOld))) {
            FILE* fp = fopen(GLQOS_CONFIG, "w");
            if (fp) {
                if (glqos) {
                    fprintf(fp, "%s", glqos);
                }
                fclose(fp);
                if (!gTest) {
                    ExecuteCommand("/etc/init.d/glqos restart", NULL);
                }
            }
        }
        if (glqosOld) {
            free(glqosOld);
        }
        glqosOld = glqos;
        
        // Free qos structure
        while (qstart) {
            Qos_t* qp = qstart->next;
            free(qstart);
            qstart = qp;
        }

        FreeUsage(usage);
        sleep(DEFAULT_SAMPLING_PERIOD);
        LoggerCheckRotate();
    }
    
  errorExit:
	Logger(LOG_INFO, "nednet2bqos2: Done");
    ApiQuit();
	return 0;
}
