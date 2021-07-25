//*******************************************************************
// File:  nednet2bcommon.c
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

int gLogLevel = (LOG_ERROR | LOG_WARN);

static String sUrl = "";
static FILE* sLogFile = NULL;
static String sAccountId = "ebfad4b9-e3d6-4159-b78a-f944f0ed655e";
static String sAuthorizationId = "26e40b9c-9846-4ee8-a54c-1f7844e2d6b2";
static int sSequenceNumber = 0;

int gRemoteLogEnable = 0;
String gPhoneNumber = "";
String gIMEI = "";
String gMAC = "";
String gUpTime = "";
String gICCID = "";
String gModel = "";
String gModem = "";
String gMAC5 = "";

int (*gPostResponse)(Response_t* rp) = NULL;

static String sVersion = NULL;
static Int64s sAPIDeltaTime = 0;
static Int64s sAPICount = 0;

static String sLogDay = NULL;
static String sLogFileName = NULL;
static String sProgramName = NULL;
static time_t sLogRotationCheck = 0;

#ifdef TEST
int sTest = TRUE;
#define LOG_PATH ""
#define DHCP_LEASES          "dhcp.leases"
#define DHCP_LEASES_SCHOOL   "dhcp.leases.lan"
#define DHCP_LEASES_GENERAL  "dhcp.leases.guest"
#else
int sTest = FALSE;
#define LOG_PATH "/tmp/"
#define DHCP_LEASES          "/tmp/dhcp.leases"
#define DHCP_LEASES_SCHOOL   "/tmp/dhcp.leases.lan"
#define DHCP_LEASES_GENERAL  "/tmp/dhcp.leases.guest"
#endif

//*******************************************************************
// ApiInit
//-------------------------------------------------------------------
void ApiInit(String url) {
    sUrl = StrDuplicate(url);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    gRemoteLogEnable = 1;
    
    /* Initialize up time */
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = gmtime(&rawtime);
    char buf[80];
    strftime(buf, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
    gUpTime = StrDuplicate(buf);    
}

//*******************************************************************
// ApiQuit
//-------------------------------------------------------------------
void ApiQuit() {
    curl_global_cleanup();
}

//*******************************************************************
// ParseLease
//-------------------------------------------------------------------
Lease_t* ParseLease(String bp) {
    Lease_t* lp = NULL;
    String sp;
    
    Logger(LOG_DEBUG, "nednet2bqos: Parsing '%s'", bp);
    lp = malloc(sizeof(*lp));
    if (!lp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not allocate lease structure");
        goto errorExit;
    }
    memset(lp, 0, sizeof(*lp));
    
    sp = strtok(bp, " ");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse time");
        lp = NULL;
        goto errorExit;
    }
    lp->timestamp = atoi(sp);
    
    sp = strtok(NULL, " ");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse mac");
        lp = NULL;
        goto errorExit;
    }
    lp->mac = StrDuplicate(sp);

    sp = strtok(NULL, " ");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse ip");
        lp = NULL;
        goto errorExit;
    }
    lp->ip = StrDuplicate(sp);

    sp = strtok(NULL, " ");
    if (!sp) {
        Logger(LOG_ERROR, "nednet2bqos: Could not parse name");
        lp = NULL;
        goto errorExit;
    }
    lp->name = StrDuplicate(sp);

  errorExit:
    return lp;
}

//*******************************************************************
// GetLeases
//-------------------------------------------------------------------
Lease_t* GetLeases() {
    Lease_t* leasesStart = NULL;
    Lease_t* leasesEnd = NULL;
    Lease_t* lease;
    Lease_t* lp;
    FILE* fp = NULL;
    char buf[256];
    
    fp = fopen(DHCP_LEASES, "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            lease = ParseLease(buf);
            if (lease) {
                lease->school = TRUE;
                if (leasesEnd) {
                    leasesEnd->next = lease;
                    leasesEnd = lease;
                } else {
                    leasesStart = leasesEnd = lease;
                }
            }
        }
        fclose(fp);
    }
    
    fp = fopen(DHCP_LEASES_SCHOOL, "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            lease = ParseLease(buf);
            if (lease) {
                // Check if duplicate mac
                for (lp = leasesStart; lp; lp = lp->next) {
                    if (!strcasecmp(lease->mac, lp->mac)) {
                        // Check if newer
                        if (lease->timestamp > lp->timestamp) {
                            lp->timestamp = lease->timestamp;
                            lp->school = TRUE;
                        } else {
                            lp = NULL;
                        }
                        break;
                    }
                }
                if (!lp) {
                    if (leasesEnd) {
                        leasesEnd->next = lease;
                        leasesEnd = lease;
                    } else {
                        leasesStart = leasesEnd = lease;
                    }
                }
            }
        }
        fclose(fp);
    }
    
    fp = fopen(DHCP_LEASES_GENERAL, "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            lease = ParseLease(buf);
            if (lease) {
                // Check if duplicate mac
                for (lp = leasesStart; lp; lp = lp->next) {
                    if (!strcasecmp(lease->mac, lp->mac)) {
                        // Check if newer
                        if (lease->timestamp > lp->timestamp) {
                            lp->timestamp = lease->timestamp;
                            lp->school = FALSE;
                        } else {
                            lp = NULL;
                        }
                        break;
                    }
                }
                if (!lp) {
                    if (leasesEnd) {
                        leasesEnd->next = lease;
                        leasesEnd = lease;
                    } else {
                        leasesStart = leasesEnd = lease;
                    }
                }
            }
        }
        fclose(fp);
    }
  errorExit:
    return leasesStart;
}

//*******************************************************************
// GetCurrentRSS
//-------------------------------------------------------------------
long GetCurrentRSS() {
    long rss = 0L;
    FILE* fp = NULL;
    if ((fp = fopen("/proc/self/statm", "r")) == NULL) {
        return (size_t)0L;      /* Can't open? */
    }
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return (size_t)0L;      /* Can't read? */
    }
    fclose(fp);
    return (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
}

//*******************************************************************
// LoggerInit
//-------------------------------------------------------------------
int LoggerInit(String program) {
    time_t rawtime;
    struct tm* timeinfo;
    char buf[80];
    
    sProgramName = program;
    sprintf(buf, "%s%s.dbg", LOG_PATH, sProgramName);
    StrReplace(&sLogFileName, buf);
    sLogFile = fopen(sLogFileName, "a+");
    if (!sLogFile) {
        Logger(LOG_ERROR, "Error opening log file.");
    } else {
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(buf, sizeof(buf), "%Y-%m-%d", timeinfo);
        StrReplace(&sLogDay, buf);
    }
    
    return ERR_OK;
}

//*******************************************************************
// LoggerCheckRotate()
//-------------------------------------------------------------------
int LoggerCheckRotate() {
    time_t rawtime;
    struct tm* timeinfo;
    char buf[80];

    rawtime = time(NULL);
    if (!sLogRotationCheck || (rawtime - sLogRotationCheck) > 60) {
        sLogRotationCheck = rawtime;
        timeinfo = localtime(&rawtime);
        strftime(buf, sizeof(buf), "%Y-%m-%d", timeinfo);
        if (strcmp(sLogDay, buf)) {
            // Rotate
            fclose(sLogFile);
            sLogFile = NULL;
            sprintf(buf, "%s%s.dbg.sav", LOG_PATH, sProgramName);
            remove(buf);
            rename(sLogFileName, buf);
            LoggerInit(sProgramName);
        }
    }
    
    return ERR_OK;
}

//*******************************************************************
// Logger
//-------------------------------------------------------------------
void Logger(int level, String format, ...) {
    va_list argp;
    va_start(argp, format);
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];
    String text = NULL;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
    
    String levelString;
    switch (level) {
        case LOG_ERROR:
            levelString = "E";
            break;
        case LOG_WARN:
            levelString = "W";
            break;
        case LOG_INFO:
            levelString = "I";
            break;
        case LOG_DEBUG:
            levelString = "D";
            break;
        default:
            levelString = "?";
            break;
    }
    /*text = malloc(4096);
    if (!text) {
        if (sLogFile) {
            fprintf(sLogFile, "%s %s %s\n", buffer, levelString,  "Could not allocate buffer");
            fflush(sLogFile);
        } else {
            printf("%s %s %s\n", buffer, levelString, "Could not allocate buffer");
        }
        goto errorExit;
    }*/
    vasprintf(&text, format, argp);
    if (sLogFile) {
        fprintf(sLogFile, "%s %s %s\n", buffer, levelString, text);
        fflush(sLogFile);
    } else {
        printf("%s %s %s\n", buffer, levelString, text);
    }
    if (level & gLogLevel && gRemoteLogEnable) {
        MessageLog(level, text);
    }
  errorExit:
    if (text) {
        free(text);
    }
    va_end(argp);
}

//*******************************************************************
// Parameters Init
//-------------------------------------------------------------------
int ParametersInit(Parameters_t* pp) {
    pp->first = NULL;
    pp->last = NULL;
    return ERR_OK;
}

//*******************************************************************
// Parameters Add
//-------------------------------------------------------------------
int ParametersAdd(Parameters_t* pp, String key, String value) {
    int status = ERR_ERROR;
    Parameter_t* dp = NULL;
    
    dp = (Parameter_t*)malloc(sizeof(*dp));
    if (!dp) {
        goto errorExit;
    }
    memset(dp, 0, sizeof(*dp));
    strcpy(dp->key, key);
    strcpy(dp->value, value);

    if (pp->last) {
        pp->last->next = dp;
        pp->last = dp;
    } else {
        pp->first = pp->last = dp;
    }
    
    status = ERR_OK;
    dp = NULL;
  errorExit:
    if (dp) {
        free(dp);
    }
    return status;
}

//*******************************************************************
// Parameters Add Int
//-------------------------------------------------------------------
int ParametersAddInt(Parameters_t* pp, String key, int value) {
    int status = ERR_ERROR;
    Parameter_t* dp = NULL;
    
    dp = (Parameter_t*)malloc(sizeof(*dp));
    if (!dp) {
        goto errorExit;
    }
    memset(dp, 0, sizeof(*dp));
    strcpy(dp->key, key);
    sprintf(dp->value, "%d", value);

    if (pp->last) {
        pp->last->next = dp;
        pp->last = dp;
    } else {
        pp->first = pp->last = dp;
    }
    
    status = ERR_OK;
    dp = NULL;
  errorExit:
    if (dp) {
        free(dp);
    }
    return status;
}

//*******************************************************************
// Parameters Add Long
//-------------------------------------------------------------------
int ParametersAddLong(Parameters_t* pp, String key, Int64s value) {
    int status = ERR_ERROR;
    Parameter_t* dp = NULL;
    
    dp = (Parameter_t*)malloc(sizeof(*dp));
    if (!dp) {
        goto errorExit;
    }
    memset(dp, 0, sizeof(*dp));
    strcpy(dp->key, key);
    sprintf(dp->value, "%lld", value);

    if (pp->last) {
        pp->last->next = dp;
        pp->last = dp;
    } else {
        pp->first = pp->last = dp;
    }
    
    status = ERR_OK;
    dp = NULL;
  errorExit:
    if (dp) {
        free(dp);
    }
    return status;
}

//*******************************************************************
// Parameters Add Array
//-------------------------------------------------------------------
int ParametersAddArray(Parameters_t* pp, String arrayName) {
    int status = ERR_ERROR;
    Parameter_t* dp = NULL;
    
    dp = (Parameter_t*)malloc(sizeof(*dp));
    if (!dp) {
        goto errorExit;
    }
    memset(dp, 0, sizeof(*dp));
    dp->paramTypeArray = TRUE;
    strcpy(dp->key, arrayName);

    if (pp->last) {
        pp->last->next = dp;
        pp->last = dp;
    } else {
        pp->first = pp->last = dp;
    }
    
    status = ERR_OK;
    dp = NULL;
  errorExit:
    if (dp) {
        free(dp);
    }
    return status;
}

//*******************************************************************
// Parameters Add Long to Array
//-------------------------------------------------------------------
int ParametersAddLongToArray(Parameters_t* pp, String arrayName, String key, Int64s value) {
    int status = ERR_ERROR;
    Parameter_t* dp = NULL;
    Parameter_t* p;
    
    // Search for array
    for (p = pp->first; p; p = p->next) {
        if (p->paramTypeArray && !strcmp(p->key, arrayName)) {
            break;
        }
    }
    if (!p) {
        Logger(LOG_DEBUG, "Could not find array %s", arrayName);
        goto errorExit;
    }
    
    // Fill in new element
    dp = (Parameter_t*)malloc(sizeof(*dp));
    if (!dp) {
        goto errorExit;
    }
    memset(dp, 0, sizeof(*dp));
    strcpy(dp->key, key);
    sprintf(dp->value, "%lld", value);
  
    // Link in parameter
    if (p->arrayLast) {
        p->arrayLast->next = dp;
        p->arrayLast = dp;
    } else {
        p->arrayFirst = p->arrayLast = dp;
    }
    
    status = ERR_OK;
    dp = NULL;
  errorExit:
    if (dp) {
        free(dp);
    }
    return status;
}

//*******************************************************************
// Parameters Free
//-------------------------------------------------------------------
int ParametersFree(Parameters_t* pp) {
    Parameter_t* p;
    
    for (p = pp->first; p; ) {
        Parameter_t* next = p->next;
        if (p->paramTypeArray) {
            Parameter_t* ap;
            for (ap = p->arrayFirst; ap; ) {
                Parameter_t* nexta = ap->next;
                free(ap);
                ap = nexta;
            }
        }
        free(p);
        p = next;
    }
    return ERR_OK;
}

//*******************************************************************
// Message Construct
//-------------------------------------------------------------------
Message_t* MessageConstruct(
    String          messageId,
    Parameters_t*   parameters
) {
    Message_t*  mp = NULL;
    
    mp = (Message_t*)malloc(sizeof(Message_t));
    if (!mp) {
        Logger(LOG_ERROR, "Error allocating message structure");
        return mp;
    }
    mp->accountId = sAccountId;
    mp->authorizationId = sAuthorizationId;
    mp->sequenceNumber = ++sSequenceNumber;
    mp->timestamp = TimeMsec();
    mp->MAC = gMAC;
    mp->messageId = messageId;
    mp->parameters.first = parameters->first;
    mp->parameters.last = parameters->last;
    
    return mp;
}

//*******************************************************************
// ResponseParse
//-------------------------------------------------------------------
int ResponseParse(Response_t* rp, String sp) {
    int status = ERR_ERROR;
    cJSON* json = NULL;
    cJSON* item;
    
    // JSON decode
    rp->sequenceNumber = -1;
    rp->timestamp = -1;
    rp->MAC = NULL;
    rp->messageId = NULL;
    rp->status = -1;
    rp->data = NULL;
    rp->json = NULL;
    
    json = cJSON_Parse(sp);
    if (!json) {
        goto errorExit;
    }
    
    item = cJSON_GetObjectItemCaseSensitive(json, "sequenceNumber");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        rp->sequenceNumber = atoi(item->valuestring);
    }
    
    item = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        rp->timestamp = atoll(item->valuestring);
    }
    
    item = cJSON_GetObjectItemCaseSensitive(json, "MAC");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        rp->MAC = item->valuestring;
    }
    
    item = cJSON_GetObjectItemCaseSensitive(json, "messageId");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        rp->messageId = item->valuestring;
    }
    
    item = cJSON_GetObjectItemCaseSensitive(json, "status");
    if (cJSON_IsNumber(item))
    {
        rp->status = (int)(item->valuedouble);
    }
    
    item = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (cJSON_IsObject(item))
    {
        rp->data = item->child;
    }
    
    rp->json = json;
    json = NULL;
    status = ERR_OK;
    
  errorExit:
    if (json) {
        cJSON_Delete(json);
    }
    return status;
}

//*******************************************************************
// ResponseFree
//-------------------------------------------------------------------
int ResponseFree(Response_t* rp) {
    if (rp && rp->json) {
        cJSON_Delete(rp->json);
        rp->json = NULL;
    }
    return ERR_OK;
}

//*******************************************************************
// JSONReadAsInt
//-------------------------------------------------------------------
int JSONReadAsInt(cJSON* jp) {
    int v = 0;
    
    if (jp) {
        if (cJSON_IsString(jp) && jp->valuestring) {
            v = atoi(jp->valuestring);
        } else if (cJSON_IsNumber(jp)) {
            v = (int)(jp->valuedouble);
        }
    }
    return v;
}

//*******************************************************************
// DefaultPostResponse
//-------------------------------------------------------------------
static int DefaultPostResponse(Response_t* response) {
    if (!strcmp(response->messageId, "log")) {
        // No processing required
    } else {
        Logger(LOG_ERROR, "Invalid messageId '%s'", response->messageId);
    }
    
  errorExit:
    return ERR_OK;
}
  
//*******************************************************************
// PostResponse
//-------------------------------------------------------------------
static size_t PostResponse(
    void* buffer, 
    size_t size, 
    size_t nmemb,
    void* userdata,
    int responseFlag
) {
    String pbuffer = NULL;
    Response_t response;
    response.json = NULL;
    Logger(LOG_DEBUG, "PostResponse(%s)", responseFlag ? "TRUE" : "FALSE");
    
    pbuffer = malloc(size * nmemb + 1);
    if (!pbuffer) {
        Logger(LOG_ERROR, "Could not allocate pbuffer");
        goto errorExit;
    }
    memcpy(pbuffer, buffer, size * nmemb);
    pbuffer[size * nmemb] = 0;
    Logger(LOG_DEBUG, "Response: %s", pbuffer);
    
    if (ResponseParse(&response, pbuffer)) {
        goto errorExit;
    }
    
    Int64s deltaTime = TimeMsec();
    deltaTime -= response.timestamp;
    sAPIDeltaTime += deltaTime;
    sAPICount++;

    if (response.status) {
        Logger(LOG_ERROR, "Error %d for messageId='%s'", response.status, response.messageId);
        goto errorExit;
    }
    
    if (responseFlag) {
        if (gPostResponse) {
            (*gPostResponse)(&response);
        } else {
            DefaultPostResponse(&response);
        }
    }

  errorExit:
    if (pbuffer) {
        free(pbuffer);
    }
    ResponseFree(&response);
    return size * nmemb;
}
    
//*******************************************************************
// PostResponseFull
//-------------------------------------------------------------------
static size_t PostResponseFull(
    void* buffer, 
    size_t size, 
    size_t nmemb,
    void* userdata
) {
    return PostResponse(buffer, size, nmemb, userdata, TRUE);
}
    
//*******************************************************************
// PostResponseLimit
//-------------------------------------------------------------------
static size_t PostResponseLimit(
    void* buffer, 
    size_t size, 
    size_t nmemb,
    void* userdata
) {
    return PostResponse(buffer, size, nmemb, userdata, FALSE);
}
    
//*******************************************************************
// MessagePost
//-------------------------------------------------------------------
int MessagePost(Message_t* mp, int responseFlag) {
    int status = ERR_ERROR;
    CURL* curl = NULL;
    int res;
    String  postFields = NULL;
    char buf[1200];
    int count;
    
    curl = curl_easy_init();
    if (!curl) {
        Logger(LOG_ERROR, "Could not init curl");
        goto errorExit;
    }

    postFields = StrConcat(postFields, "{");
    sprintf(buf, "\"accountId\":\"%s\"", mp->accountId);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"authorizationId\":\"%s\"", mp->authorizationId);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"sequenceNumber\":\"%d\"", mp->sequenceNumber);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"timestamp\":\"%lld\"", mp->timestamp);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"MAC\":\"%s\"", mp->MAC);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"messageId\":\"%s\"", mp->messageId);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"apiCount\":\"%lld\"", sAPICount);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"apiTotalMsec\":\"%lld\"", sAPIDeltaTime);
    postFields = StrConcat(postFields, buf);
    sprintf(buf, ",\"parameters\":{");
    postFields = StrConcat(postFields, buf);
    count = 0;
    for (Parameter_t* p = mp->parameters.first; p; p = p->next) {
        if (count++ > 0) {
            postFields = StrConcat(postFields, ",");
        }
        if (p->paramTypeArray) {
            sprintf(buf, "\"%s\":{", p->key);
            postFields = StrConcat(postFields, buf);
            int count2 = 0;
            for (Parameter_t* p2 = p->arrayFirst; p2; p2 = p2->next) {
                if (count2++ > 0) {
                    postFields = StrConcat(postFields, ",");
                }
                sprintf(buf, "\"%s\":\"%s\"", p2->key, p2->value);
                postFields = StrConcat(postFields, buf);
            }
            postFields = StrConcat(postFields, "}");
        } else {
            sprintf(buf, "\"%s\":\"%s\"", p->key, p->value);
            postFields = StrConcat(postFields, buf);
        }
    }
    postFields = StrConcat(postFields, "}}");
    Logger(LOG_DEBUG, postFields);
    
    curl_easy_setopt(curl, CURLOPT_URL, sUrl);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields);
    if (responseFlag) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostResponseFull);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostResponseLimit);
    }
    
    //Logger(LOG_DEBUG, "Performing curl");
    res = curl_easy_perform(curl);
    //Logger(LOG_DEBUG, "Back from curl");
    
    /* Check for errors */ 
    if (res != CURLE_OK) {
        Logger(LOG_ERROR, "Error from curl");
        goto errorExit;
    }
    
    status = ERR_OK;
    
  errorExit:
    if (postFields) {
        free(postFields);
    }
    if (curl) {
        curl_easy_cleanup(curl);
    }
    return status;
}

//*******************************************************************
// MessageLog
//-------------------------------------------------------------------
int MessageLog(int level, String log) {
    int             status = ERR_ERROR;
    Message_t*      mp = NULL;
    Parameters_t    parameters;
    
    gRemoteLogEnable = 0;
    ParametersInit(&parameters);
    if (ParametersAddInt(&parameters, "logLevel", level)) {
        goto errorExit;
    }
    if (ParametersAdd(&parameters, "log", log)) {
        goto errorExit;
    }
    
    mp = MessageConstruct("log", &parameters);
    if (!mp) {
        goto errorExit;
    }
    if (MessagePost(mp, FALSE)) {
        goto errorExit;
    }
    
    status = ERR_OK;
    
  errorExit:
    ParametersFree(&parameters);
    if (mp) {
        free(mp);
    }
    gRemoteLogEnable = 1;
    return status;
}

//*******************************************************************
// MacRead
//-------------------------------------------------------------------
int MacRead() {
    if (sTest) {
        gMAC = DEFAULT_MAC;
        return  ERR_OK;
    } else {
        int status = ERR_ERROR;
        FILE*   p = NULL;
        char    buf[80];
        char    buf2[80];
        String  rp = NULL;
        String  rp2 = NULL;
        
        buf[0] = 0;
        if (ExecuteCommand("ifconfig eth0", &rp)) {
            Logger(LOG_DEBUG, "Unable to read eth0");
        } else {
            String sp = strstr(rp, "HWaddr ");
            if (sp) {
                sp += 7;
                strncpy(buf, sp, 17);
                buf[17] = 0;
            }
        }
        
        buf2[0] = 0;
        if (ExecuteCommand("ifconfig eth1", &rp2)) {
            Logger(LOG_DEBUG, "Unable to read eth1");
        } else {
            String sp = strstr(rp2, "HWaddr ");
            if (sp) {
                sp += 7;
                strncpy(buf2, sp, 17);
                buf2[17] = 0;
            }
        }
        
        if (buf[0] && buf2[0]) {
            // Pick lower MAC address
            if (strcmp(buf, buf2) <= 0) {
                gMAC = StrDuplicate(buf);
            } else {
                gMAC = StrDuplicate(buf2);
            }
        } else if (buf[0]) {
            gMAC = StrDuplicate(buf);
        } else if (buf2[0]) {
            gMAC = StrDuplicate(buf2);
        } else {
            goto errorExit;
        }       
        
        char mac5[6];
        int l = strlen(gMAC);
        if (l >= 7) {
            mac5[0] = gMAC[l - 7];
            mac5[1] = gMAC[l - 5];
            mac5[2] = gMAC[l - 4];
            mac5[3] = gMAC[l - 2];
            mac5[4] = gMAC[l - 1];
            mac5[5] = 0;
        } else {
            mac5[0] = 0;
        }
        gMAC5 = StrDuplicate(mac5);

        status = ERR_OK;

      errorExit:
        if (rp) {
            free(rp);
        } 
        if (rp2) {
            free(rp2);
        } 
        return status;
    }
}

//*******************************************************************
// ReadTextFile
//-------------------------------------------------------------------
String ReadTextFile(String filename) {
    String sp = NULL;
    long length;
    FILE* fp = NULL;

    fp = fopen(filename, "r");
    if (fp)
    {
        fseek (fp, 0, SEEK_END);
        length = ftell(fp);
        fseek (fp, 0, SEEK_SET);
        sp = malloc(length + 1);
        if (sp)
        {
            *sp = 0;
            if (length) {
                fread(sp, 1, length, fp);
            }
            sp[length] = 0;
        }
    }

    if (fp) {
        fclose(fp);
    }
    return sp;
}

//*******************************************************************
// WriteTextFile
//-------------------------------------------------------------------
int WriteTextFile(String filename, String data) {
    int status = ERR_ERROR;
    FILE* fp = NULL;
    
    fp = fopen(filename, "w");
    if (fp)
    {
        fputs(data, fp);
    }    
    
    status = ERR_OK;
    
  errorExit:
    if (fp) {
        fclose(fp);
    }
    return status;
}

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
// ParseWordsFree
//-------------------------------------------------------------------
int ParseWordsFree(String* words) {
    if (words) {
        for (int i = 0; words[i]; i++) {
            free(words[i]);
        }
        free(words);
    }
    
    return ERR_OK;
}

//*******************************************************************
// ParseWords
//-------------------------------------------------------------------
String* ParseWords(String sp, char delimiter) {
    String* words = NULL;
    int count = 0;
    
    if (sp && strlen(sp)) {
        String start = sp;
        String sp2;
        for (sp2 = sp; *sp2; sp2++) {
            if (*sp2 == delimiter) {
                int length = sp2 - start;
                String new = malloc(length + 1);
                if (!new) {
                    ParseWordsFree(words);
                    words = NULL;
                    goto errorExit;
                }
                strncpy(new, start, length);
                new[length] = 0;
                String* nwords = malloc(sizeof(String) * (count + 2));
                if (!nwords) {
                    free(new);
                    ParseWordsFree(words);
                    words = NULL;
                    goto errorExit;
                }
                memcpy(nwords, words, sizeof(String) * count);
                nwords[count] = new;
                nwords[count + 1] = NULL;
                words = nwords;
                count++;
                start = sp2 + 1;
            }
        }
        int length = sp2 - start;
        if (length) {
            String new = malloc(length + 1);
            if (!new) {
                ParseWordsFree(words);
                words = NULL;
                goto errorExit;
            }
            strncpy(new, start, length);
            new[length] = 0;
            String* nwords = malloc(sizeof(String) * (count + 2));
            if (!nwords) {
                free(new);
                ParseWordsFree(words);
                words = NULL;
                goto errorExit;
            }
            memcpy(nwords, words, sizeof(String) * count);
            nwords[count] = new;
            nwords[count + 1] = NULL;
            words = nwords;
        }
    }
    
  errorExit:
    return words;
}

//*******************************************************************
// JSONGetObject
//-------------------------------------------------------------------
cJSON* JSONGetObject(cJSON* json, String name) {
    cJSON* jp = NULL;
    cJSON* item;
    
    item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (item && cJSON_IsObject(item))
    {
        jp = item;
    }
    return jp;
}

//*******************************************************************
// JSONGetString
//-------------------------------------------------------------------
String JSONGetString(cJSON* json, String name) {
    String sp = NULL;
    cJSON* item;
    
    item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (item && cJSON_IsString(item))
    {
        sp = item->valuestring;
    }
    return sp;
}

//*******************************************************************
// StrDuplicate
//-------------------------------------------------------------------
String StrDuplicate(String inSp) {
    String sp = NULL;

    if (inSp)
    {
        int length = strlen(inSp);
        sp = malloc(length + 1);
        if (sp) {
            strcpy(sp, inSp);
        }
    }
    return sp;
}

//*******************************************************************
// StrReplace
//-------------------------------------------------------------------
int StrReplace(String* spp, String newString) {
    if (*spp)
    {
        free(*spp);
        *spp = NULL;
    }
    *spp = StrDuplicate(newString);
    return ERR_OK;
}

//*******************************************************************
// StrConcat
//-------------------------------------------------------------------
String StrConcat(String base, String toAdd) {
    String sp = NULL;

    if (toAdd)
    {
        if (base) {
            sp = malloc(strlen(base) + strlen(toAdd) + 1);
            if (sp) {
                strcpy(sp, base);
                strcat(sp, toAdd);
                free(base);
            }
        } else {
            sp = malloc(strlen(toAdd) + 1);
            if (sp) {
                strcpy(sp, toAdd);
            }
        }
    } else {
        sp = base;
    }
    
    return sp;
}

//*******************************************************************
// ModelRead
//-------------------------------------------------------------------
int ModelRead() {
    int status = ERR_ERROR;
    String  bp = NULL;
    cJSON* json = NULL;

    if (sTest) {
        gModel = DEFAULT_MODEL;
    } else {
        bp = ReadTextFile("/etc/board.json");
        if (bp) {
            json = cJSON_Parse(bp);
            if (json) {
                cJSON* jp = JSONGetObject(json, "model");
                if (jp) {
                    String sp = JSONGetString(jp, "id");
                    if (sp) {
                        gModel = StrDuplicate(sp);
                        //Logger(LOG_INFO, "Model: '%s'", gModel);
                    } else {
                        Logger(LOG_ERROR, "Unable to find 'id' in board.json");
                    }
                }
            } else {
                Logger(LOG_ERROR, "Unable to parse board.json");
            }
        } else {
            Logger(LOG_ERROR, "Unable to read board.json");
        }
    }
    
  errorExit:
    if (json) {
        cJSON_Delete(json);
    }
    if (bp) {
        free(bp);
    }
    return status;
}

//*******************************************************************
// SimRead
//-------------------------------------------------------------------
int SimRead() {
    int status = ERR_ERROR;
    char    buf[80];
    String  bp = NULL;
    cJSON* json = NULL;
    String rp = NULL;
    String sp;
    int sport = -1;
    
    // Get model
    ModelRead();
    
    if (sTest) {
        gPhoneNumber = DEFAULT_PHONE_NUMBER;
        gIMEI = DEFAULT_IMEI;
        gICCID = DEFAULT_ICCID;
        status = ERR_OK;
    } else {
        // Get phone number
        if (ExecuteCommand("uqmi -d /dev/cdc-wdm0 --get-msisdn", &rp)) {
            Logger(LOG_ERROR, "Unable to get phone number");
            goto errorExit;
        }
        sp = strstr(rp, "\"");
        if (sp) {
            String sp2 = strstr(sp + 1, "\"");
            if (sp2) {
                strncpy(buf, sp + 1, sp2 - sp - 1);
                buf[sp2 - sp - 1] = 0;
                gPhoneNumber = StrDuplicate(buf);
                //Logger(LOG_INFO, "Phone number: '%s'", gPhoneNumber);
            }
        }
        free(rp);
        rp = NULL;
        
        // Get IMEI
        if (ExecuteCommand("uqmi -d /dev/cdc-wdm0 --get-imei", &rp)) {
            Logger(LOG_ERROR, "Unable to get IMEI");
            goto errorExit;
        }
        sp = strstr(rp, "\"");
        if (sp) {
            String sp2 = strstr(sp + 1, "\"");
            if (sp2) {
                strncpy(buf, sp + 1, sp2 - sp - 1);
                buf[sp2 - sp - 1] = 0;
                gIMEI = StrDuplicate(buf);
                //Logger(LOG_INFO, "IMEI: '%s'", gIMEI);
            }
        }
        free(rp);
        rp = NULL;
        
        // Get ICCID
        sport = open(MODEM_PORT, O_RDWR);
        if (sport < 0) {
            Logger(LOG_ERROR, "Unable to open modem port");
            goto errorExit;
        }
        String msg = "AT+QCCID\r";
        if (write(sport, msg, strlen(msg)) != strlen(msg)) {
            Logger(LOG_ERROR, "Error writing command to modem");
            goto errorExit;
        }
        int startTime = time(NULL);
        int index = 0;
        int found = FALSE;
        while ((time(NULL) - startTime) < 3) {
            char c;
            int n = read(sport, &c, 1);
            if (n > 0) {
                if (c < 0x20) {
                    buf[index] = 0;
                    index = 0;
                    sp = strstr(buf, "+QCCID: ");
                    if (sp) {
                        sp += 8;
                        String sp2 = sp;
                        while (isdigit(*sp2)) {
                            sp2++;
                        }
                        *sp2 = 0;
                        gICCID = StrDuplicate(sp);
                        found = TRUE;
                        break;
                    }
                } else {
                    buf[index++] = c;
                }
                if (index >= (sizeof(buf) - 1)) {
                    index = 0;
                }
            } else {
                usleep(10000);
            }
        }
        if (!found) {
            Logger(LOG_ERROR, "Error reading ICCID from modem");
        }
        close(sport);
        sport = -1;
        //Logger(LOG_INFO, "ICCID: '%s'", gICCID);
        
        // Get Model
        sport = open(MODEM_PORT, O_RDWR);
        if (sport < 0) {
            Logger(LOG_ERROR, "Unable to open modem port");
            goto errorExit;
        }
        msg = "ATI\r";
        if (write(sport, msg, strlen(msg)) != strlen(msg)) {
            Logger(LOG_ERROR, "Error writing command to modem");
            goto errorExit;
        }
        startTime = time(NULL);
        index = 0;
        found = FALSE;
        while ((time(NULL) - startTime) < 3) {
            char c;
            int n = read(sport, &c, 1);
            if (n > 0) {
                if (c < 0x20) {
                    buf[index] = 0;
                    index = 0;
                    if (strstr(buf, "EP06")) {
                        gModem = "EP06";
                        found = TRUE;
                        break;
                    } else if (strstr(buf, "EC25")) {
                        gModem = "EC25";
                        found = TRUE;
                        break;
                    }
                } else {
                    buf[index++] = c;
                }
                if (index >= (sizeof(buf) - 1)) {
                    index = 0;
                }
            } else {
                usleep(10000);
            }
        }
        if (!found) {
            Logger(LOG_ERROR, "Error reading modem from modem");
        }
        close(sport);
        sport = -1;
        //Logger(LOG_INFO, "Modem: '%s'", gModem);
        
        status = ERR_OK;
    }

  errorExit:
    if (sport >= 0) {
        close(sport);
    }
    if (rp) {
        free(rp);
    }
    if (json) {
        cJSON_Delete(json);
    }
    if (bp) {
        free(bp);
    }
    return status;
}

//*******************************************************************
// ExecuteCommand
//-------------------------------------------------------------------
int ExecuteCommand(String command, String* rpp) {
    int status = ERR_ERROR;
    FILE* p = NULL;
    
    // Execute command
    Logger(
        LOG_INFO, 
        "ExecuteCommand: '%s' %s", 
        command, 
        rpp ? "waitResponse" : "noResponse"
    );
    p = popen(command, "r");
    if (!p) {
        Logger(LOG_ERROR, "Unable to open process");
        goto errorExit;;
    }
    if (rpp) {
        String response = malloc(MAX_VALUE_LEN);
        if (!response) {
            Logger(LOG_ERROR, "Unable to allocate memory for response");
            goto errorExit;;
        }
        *response = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), p) != NULL) {
            if (strlen(buf) > (MAX_VALUE_LEN - 1 - strlen(response))) {
                break;
            }
            String sp = response + strlen(response);
            String sp2 = buf;
            while (*sp2) {
                if (*sp2 != '\r') {
                    *sp++ = *sp2;
                }
                sp2++;
            }
            *sp = 0;
        }
        *rpp = response;
    }
    status = ERR_OK;
    
  errorExit:
    if (p) {
        status = WEXITSTATUS(pclose(p));
    }
    return status;
}

//*******************************************************************
// TimeMsec
//-------------------------------------------------------------------
Int64s TimeMsec() {
    Int64s mtime;
    struct timeval  tv;
    
    gettimeofday(&tv, NULL);
    mtime = tv.tv_sec;
    mtime *= 1000;
    mtime += tv.tv_usec / 1000;
    
    return mtime;
}

//*******************************************************************
// AddToBoot
//-------------------------------------------------------------------
int AddToBoot(String program) {
    int status = ERR_ERROR;
    String sp = NULL;
    
    if (sTest) {
        status = ERR_OK;
    } else {
        // Add boot to rc.local
        sp = ReadTextFile("/etc/rc.local");
        if (sp) {
            if (!strstr(sp, program)) {
                char buf[128];
                sprintf(buf, "\necho \"%s &\n\" >> /etc/rc.local", program);
                if (ExecuteCommand(buf, NULL)) {
                    goto errorExit;
                }
            }
        }
        status = ERR_OK;
    }

  errorExit:
    if (sp) {
        free(sp);
    }
    return status;
}

//*******************************************************************
// Get Version
//-------------------------------------------------------------------
String GetVersion() {
    if (sTest) {
        return "TEST_VERSION";
    } else {
        if (sVersion) {
            return sVersion;
        }
        String sp = ReadTextFile("/root/version");
        if (!sp) {
            Logger(LOG_ERROR, "Could not read version file");
            return "Unknown";
        }
        String sp2 = strtok(sp, "\n");
        if (!sp2 || !strlen(sp2)) {
            Logger(LOG_ERROR, "Could not parse version file");
            free(sp);
            return "Unknown";
        }
        sVersion = StrDuplicate(sp2);
        free(sp);
        return sVersion;
    }
}

//*******************************************************************
// ConfigGetApi
//-------------------------------------------------------------------
String ConfigGetApi() {
    return ConfigRead("/root/config.json", "apiBase");
}

//*******************************************************************
// ConfigRead
//-------------------------------------------------------------------
String ConfigRead(
    String configFile,
    String key
) {
    String value = NULL;
    String bp = NULL;
    cJSON* json = NULL;
    
    bp = ReadTextFile(configFile);
    if (bp) {
        json = cJSON_Parse(bp);
        if (json) {
            String sp = JSONGetString(json, key);
            if (sp) {
                value = StrDuplicate(sp);
            }
        } else {
            Logger(LOG_ERROR, "ConfigRead: Error parsing config file");
        }
    }
    
    if (bp) {
        free(bp);
    }
    if (json) {
        cJSON_Delete(json);
    }
    return value;
}

//*******************************************************************
// ConfigWrite
//-------------------------------------------------------------------
int ConfigWrite(
    String configFile,
    String key,
    String value
) {
    int status = ERR_ERROR;
    String bp = NULL;
    cJSON* json = NULL;
    cJSON* newItem = NULL;
    String newJson = NULL;
    
    // Create new JSON string
    if (!value) {
        value = "";
    }
    newItem = cJSON_CreateString(value);
    if (!newItem) {
        Logger(LOG_ERROR, "ConfigWrite: Error creating new string");
        goto errorExit;
    }
    
    // Check if file exists
    bp = ReadTextFile(configFile);
    if (bp) {
        // Modify existing json
        json = cJSON_Parse(bp);
        if (json) {
            String sp = JSONGetString(json, key);
            if (sp) {
                // Replace item
                cJSON_ReplaceItemInObject(json, key, newItem);
                newItem = NULL;
            } else {
                // Add item
                cJSON_AddItemToObject(json, key, newItem);
                newItem = NULL;
            }
        } else {
            Logger(LOG_ERROR, "ConfigWrite: Error parsing config");
            goto errorExit;
        }
    } else {
        // Create new json
        json = cJSON_CreateObject();
        if (json) {
            // Add item
            cJSON_AddItemToObject(json, key, newItem);
            newItem = NULL;
        } else {
            Logger(LOG_ERROR, "ConfigWrite: Error creating json object");
            goto errorExit;
        }
    }
    
    if (json) {
        newJson = cJSON_Print(json);
        if (!newJson) {
            Logger(LOG_ERROR, "ConfigWrite: Could not pring json");
            goto errorExit;
        }
        if (WriteTextFile(configFile, newJson)) {
            goto errorExit;
        }
    } else {
        Logger(LOG_ERROR, "ConfigWrite: No json to print");
        goto errorExit;
    }
    
    status = ERR_OK;
    
  errorExit:
    if (bp) {
        free(bp);
    }
    if (newJson) {
        free(newJson);
    }
    if (json) {
        cJSON_Delete(json);
    }
    if (newItem) {
        cJSON_Delete(newItem);
    }
    return status;
}