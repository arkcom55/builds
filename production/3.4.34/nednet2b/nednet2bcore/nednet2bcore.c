//*******************************************************************
// File:  nednet2bcore.c
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
#include <fcntl.h> // Contains file controls like O_RDWR
#include <ctype.h>
#include <crypt.h>

#include "cJSON.h"
#include "nednet2bcommon.h"

#define DEFAULT_REPORT_INTERVAL     (60)
#define MINIMUM_REPORT_INTERVAL     (10)
#define MAXIMUM_REPORT_INTERVAL     (600)

String gName = "nednet2bcore";
String gVersion = "nednet2bcore.3.5-2";

#define API_BASE "https://dnsapi.neducation.ca/api/"
#define API_FILE "nedNet2b.php"
static int sSessionId = 0;
static int sReportInterval = DEFAULT_REPORT_INTERVAL;

static String sLastVersion = NULL;
static String sLastModel = NULL;
static String sLastModem = NULL;
static String sLastPhone = NULL;
static String sLastIMEI = NULL;
static String sLastICCID = NULL;
static String sLastUpTime = NULL;
static String sCarrier = NULL;
static String sDDNS = NULL;
static String sSerial = NULL;
static int sLastPingResponse = 0;
static int sAcked = FALSE;
static String sWifiPassword = NULL;

#ifdef TEST
static int sTest = TRUE;
#else
static int sTest = FALSE;
#endif
  
#define FULL    (0)

//*******************************************************************
// Message: Heartbeat
//-------------------------------------------------------------------
int MessageHeartbeat() {
    int             status = ERR_ERROR;
    Message_t*      mp = NULL;
    Parameters_t    parameters;
    String          sp;
    
    Logger(LOG_DEBUG, "Message heartbeat");
    
    ParametersInit(&parameters);
    sp = GetVersion();
    /*if (!sAcked || !sLastVersion || strcmp(sLastVersion, sp)) {
        if (ParametersAdd(&parameters, "version", sp)) {
            goto errorExit;
        }
        StrReplace(&sLastVersion, sp);
    }*/
    
    // Always send versions
    if (ParametersAdd(&parameters, "version", sp)) {
        goto errorExit;
    }
    if (ParametersAdd(&parameters, "iversion", gVersion)) {
        goto errorExit;
    }
    
    if (!sAcked || !sLastModel || strcmp(sLastModel, gModel)) {
        if (ParametersAdd(&parameters, "model", gModel)) {
            goto errorExit;
        }
        StrReplace(&sLastModel, gModel);
    }
    
    if (!sAcked || !sLastModem || strcmp(sLastModem, gModem)) {
        if (ParametersAdd(&parameters, "modem", gModem)) {
            goto errorExit;
        }
        StrReplace(&sLastModem, gModem);
    }
    
    // Always send phone number
    /*if (!sAcked || !sLastPhone || strcmp(sLastPhone, gPhoneNumber)) {
        if (ParametersAdd(&parameters, "phoneNumber", gPhoneNumber)) {
            goto errorExit;
        }
        StrReplace(&sLastPhone, gPhoneNumber);
    }*/
    if (!gPhoneNumber) {
        SimRead();
    }
    if (ParametersAdd(&parameters, "phoneNumber", gPhoneNumber)) {
        goto errorExit;
    }

    if (!sAcked || !sLastIMEI || strcmp(sLastIMEI, gIMEI)) {
        if (ParametersAdd(&parameters, "IMEI", gIMEI)) {
            goto errorExit;
        }
        StrReplace(&sLastIMEI, gIMEI);
    }

    // Always send ICCID
    /*if (!sAcked || !sLastICCID || strcmp(sLastICCID, gICCID)) {
        if (ParametersAdd(&parameters, "ICCID", gICCID)) {
            goto errorExit;
        }
        StrReplace(&sLastICCID, gICCID);
    }*/
    if (ParametersAdd(&parameters, "ICCID", gICCID)) {
        goto errorExit;
    }

    if (!sAcked || !sLastUpTime || strcmp(sLastUpTime, gUpTime)) {
        if (ParametersAdd(&parameters, "upTime", gUpTime)) {
            goto errorExit;
        }
        StrReplace(&sLastUpTime, gUpTime);
    }

    if (!sAcked || !sCarrier || strcmp(sCarrier, gCarrier)) {
        if (ParametersAdd(&parameters, "carrier", gCarrier)) {
            goto errorExit;
        }
        StrReplace(&sCarrier, gCarrier);
    }

    if (!sAcked || !sDDNS || strcmp(sDDNS, gDDNS)) {
        if (ParametersAdd(&parameters, "ddns", gDDNS)) {
            goto errorExit;
        }
        StrReplace(&sDDNS, gDDNS);
    }

    if (!sAcked || !sSerial || strcmp(sSerial, gSerial)) {
        if (ParametersAdd(&parameters, "serial", gSerial)) {
            goto errorExit;
        }
        StrReplace(&sSerial, gSerial);
    }

    if (ParametersAddInt(&parameters, "sessionId", sSessionId)) {
        goto errorExit;
    }

    if (ParametersAddInt(&parameters, "reportInterval", sReportInterval)) {
        goto errorExit;
    }

    if (ParametersAddLong(&parameters, "memory", GetCurrentRSS())) {
        goto errorExit;
    }

    mp = MessageConstruct("heartbeat", &parameters);
    if (!mp) {
        goto errorExit;
    }
    Logger(LOG_DEBUG, "Sending heartbeat");
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
}

//*******************************************************************
// Message: MessageCommandResponse
//-------------------------------------------------------------------
int MessageCommandResponse(int commandReference, String response) {
    int             status = ERR_ERROR;
    Message_t*      mp = NULL;
    Parameters_t    parameters;
    
    Logger(LOG_DEBUG, "MessageCommandResponse()");
    gRemoteLogEnable = 0;
    ParametersInit(&parameters);
    if (ParametersAddInt(&parameters, "commandReference", commandReference)) {
        goto errorExit;
    }
    
    // Encode string, collapse spaces and map \r and \n
    String bp = malloc((strlen(response) * 2) + 1);
    if (!bp) {
        Logger(LOG_DEBUG, "MessageCommandResponse: Error allocating buffer");
        goto errorExit;
    }
    String rp = bp;
    int haveSpaces = FALSE;
    for (String sp = response; *sp; sp++) {
        if (*sp == ' ') {
            if (!haveSpaces) {
                *bp++ = ' ';
                haveSpaces = TRUE;
            }
        } else {
            haveSpaces = FALSE;
            if (*sp == '\n') {
                *bp++ = '\\';
                *bp++ = 'n';
            } else if (*sp == '\r') {
                *bp++ = '\\';
                *bp++ = 'r';
            } else if (*sp == '\\') {
                *bp++ = '\\';
                *bp++ = '\\';
            } else if (*sp == '"') {
                *bp++ = '\\';
                *bp++ = '"';
            } else {
                *bp++ = *sp;
            }
        }
    }
    *bp = 0;    
    
    if (ParametersAdd(&parameters, "response", rp)) {
        goto errorExit;
    }
    free(bp);
    
    mp = MessageConstruct("commandResponse", &parameters);
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
// Ping
//-------------------------------------------------------------------
int Ping() {
    String response = NULL;
    
    ExecuteCommand("ping -c 1 8.8.8.8", &response);
    if (response) {
        if (strstr(response, "1 packets received")) {
            sLastPingResponse = time(NULL);
        }
        free(response);
    }
    
    return ERR_OK;
}

//*******************************************************************
// PostResponse
//-------------------------------------------------------------------
static int PostResponse(Response_t* response) {
    if (!strcmp(response->messageId, "heartbeat")) {
        Logger(LOG_DEBUG, "Received heartbeat response");
        sAcked = TRUE;
        for (cJSON* item = response->data; item; item = item->next) {
            Logger(LOG_DEBUG, "Item key '%s', type=%d", item->string, item->type);
            if (!strcmp(item->string, "logLevel")) {
                gLogLevel = (int)(item->valuedouble);
            } else if (!strcmp(item->string, "reportInterval")) {
                int reportInterval = (int)(item->valuedouble);
                if (reportInterval >= MINIMUM_REPORT_INTERVAL && reportInterval <= MAXIMUM_REPORT_INTERVAL) {
                    if (reportInterval != sReportInterval) {
                        sReportInterval = reportInterval;                        
                    }
                } else if (!reportInterval) {
                    sReportInterval = DEFAULT_REPORT_INTERVAL;
                }
                Logger(LOG_DEBUG, "Setting report interval=%d", sReportInterval);
            } else if (!strcmp(item->string, "wifiPassword")) {
                String wifiPassword = item->valuestring;
                if (wifiPassword && strcmp(wifiPassword, sWifiPassword)) {
                    LogPermanent(gName, "New Wifi Password '%s'", wifiPassword);
                    if (!strcmp(gModel, "gl-mifi")) {
                        String command = NULL;
                        command = StrConcat(command, "uci set wireless.default_radio0.key='");
                        command = StrConcat(command, wifiPassword);
                        command = StrConcat(command, "';");
                        command = StrConcat(command, "uci set wireless.guest2g.key='");
                        command = StrConcat(command, wifiPassword);
                        command = StrConcat(command, "';");
                        command = StrConcat(command, "uci commit wireless");
                        ExecuteCommand(command, NULL);
                        free(command);
                    } else if (!strcmp(gModel, "gl-x750")) {
                        String command = NULL;
                        command = StrConcat(command, "uci set wireless.default_radio0.key='");
                        command = StrConcat(command, wifiPassword);
                        command = StrConcat(command, "';");
                        command = StrConcat(command, "uci set wireless.default_radio1.key='");
                        command = StrConcat(command, wifiPassword);
                        command = StrConcat(command, "';");
                        command = StrConcat(command, "uci set wireless.guest2g.key='");
                        command = StrConcat(command, wifiPassword);
                        command = StrConcat(command, "';");
                        command = StrConcat(command, "uci set wireless.guest5g.key='");
                        command = StrConcat(command, wifiPassword);
                        command = StrConcat(command, "';");
                        command = StrConcat(command, "uci commit wireless");
                        ExecuteCommand(command, NULL);
                        free(command);
                    }
                    StrReplace(&sWifiPassword, wifiPassword);   
                    ExecuteCommand("reboot", NULL);
                }
            } else if (!strcmp(item->string, "commands")) {
                // Should be array of commands
                for (cJSON* item2 = item->child; item2; item2 = item2->next) {
                    int commandReference;
                    int responseFlag = 0;
                    String command = NULL;
                    cJSON* item3;
                    
                    Logger(LOG_DEBUG, "Array Item key '%s', type=%d", item2->string, item2->type);
                    if (!cJSON_IsObject(item2)) {
                        Logger(LOG_ERROR, "Expecting object in command array");
                        continue;
                    }
                    
                    item3 = cJSON_GetObjectItemCaseSensitive(item2, "commandReference");
                    if (item3) {
                        Logger(LOG_DEBUG, "Found commandReference");
                        commandReference = JSONReadAsInt(item3);
                    }

                    item3 = cJSON_GetObjectItemCaseSensitive(item2, "responseFlag");
                    if (item3) {
                        Logger(LOG_DEBUG, "Found responseFlag");
                        responseFlag = JSONReadAsInt(item3);
                    }

                    item3 = cJSON_GetObjectItemCaseSensitive(item2, "command");
                    if (cJSON_IsString(item3) && (item3->valuestring != NULL))
                    {
                        Logger(LOG_DEBUG, "command");
                        command = item3->valuestring;
                    }

                    if (command) {
                        Logger(LOG_DEBUG, "commandReference=%d responseFlag=%d command='%s'", commandReference, responseFlag, command);
                        
                        if (responseFlag) {
                            String sp = NULL;
                            if (!ExecuteCommand(command, &sp)) {
                                MessageCommandResponse(commandReference, sp);
                            }
                            if (sp) {
                                free(sp);
                            }
                        } else {
                            ExecuteCommand(command, NULL);
                        }
                    } else {
                        Logger(LOG_ERROR, "No command found");
                    }
                }
            }
        }    
    } else if (!strcmp(response->messageId, "log")) {
        // No processing required
    } else {
        Logger(LOG_ERROR, "Invalid messageId '%s'", response->messageId);
    }
    
  errorExit:
    return ERR_OK;
}
    
//*******************************************************************
// main
//-------------------------------------------------------------------
int main(int argc, char **argv) {    
    int reboot = FALSE;
    
    // Add to boot sequence
    if (!sTest) {
        AddToBoot(gName);
        LogPermanent(gName, "Starting...");
    }
    
    MacRead();
    
    if (argc > 1) {
        if (!strcmp(argv[1], "-x")) {
            return 0;
        }
        
        SimRead();
        printf("Version: '%s'\n", gVersion);
        printf("    MAC: '%s'\n", gMAC);
        printf("  Model: '%s'\n", gModel);
        printf("  Modem: '%s'\n", gModem);
        printf("  Phone: '%s'\n", gPhoneNumber);
        printf("   IMEI: '%s'\n", gIMEI);
        printf("  ICCID: '%s'\n", gICCID);
        printf("CARRIER: '%s'\n", gCarrier);
        printf("    APN: '%s'\n", gAPN);
        printf("   DDNS: '%s'\n", gDDNS);
        printf(" SERIAL: '%s'\n", gSerial);
        
        return 0;
    }
    
    SimRead();
    
    if (!sTest && gAPN && strlen(gAPN)) {
        SetAPN(gAPN);
    }
    
#if FULL
    if (!sTest && gAPN && strlen(gAPN)) {
        String sp = ReadTextFile("/etc/config/network");
        if (sp) {
            if (!strstr(sp, gAPN)) {
                String command = NULL;
                command = StrConcat(command, "uci -q delete network.modem_1_1_2;");
                command = StrConcat(command, "uci set network.modem_1_1_2='interface';");
                command = StrConcat(command, "uci set network.modem_1_1_2.ifname='3g-modem';");
                command = StrConcat(command, "uci set network.modem_1_1_2.service='umts';");
                command = StrConcat(command, "uci set network.modem_1_1_2.apn='");
                command = StrConcat(command, gAPN);
                command = StrConcat(command, "';");
                command = StrConcat(command, "uci set network.modem_1_1_2.proto='3g';");
                command = StrConcat(command, "uci set network.modem_1_1_2.device='/dev/ttyUSB3';");
                command = StrConcat(command, "uci set network.modem_1_1_2.metric='40';");
                command = StrConcat(command, "uci set network.modem_1_1_2.disabled='0';");
                command = StrConcat(command, "uci commit network");
                
                ExecuteCommand(command, NULL);
                free(command);
                
                SetAPN(gAPN);
                
                command = NULL;
                command = StrConcat(command, "uci set firewall.@zone[1].network='wan wan6 modem_1_1_2';");
                command = StrConcat(command, "uci del_list firewall.@zone[1].list='wan';");
                command = StrConcat(command, "uci del_list firewall.@zone[1].list='wan6';");
                command = StrConcat(command, "uci commit firewall");
                
                ExecuteCommand(command, NULL);
                free(command);
                
                LogPermanent(gName, "Setting network '%s'", gAPN);
                reboot = TRUE;
            }
            free(sp);
        }
    }
    
    // Set up WiFi interfaces
    if (!sTest) {
        String sp = ReadTextFile("/etc/config/wireless");
        if (sp) {
            if (!strstr(sp, "NEDSchool")) {
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
                
                if (!strcmp(gModel, "gl-mifi")) {
                    String command = NULL;
                    command = StrConcat(command, "uci set wireless.default_radio0.ssid='NEDSchool-");
                    command = StrConcat(command, mac5);
                    command = StrConcat(command, "';");
                    command = StrConcat(command, "uci set wireless.guest2g.ssid='NEDGeneral-");
                    command = StrConcat(command, mac5);
                    command = StrConcat(command, "';");
                    command = StrConcat(command, "uci set wireless.guest2g.disabled='0';");
                    command = StrConcat(command, "uci commit wireless");
                    ExecuteCommand(command, NULL);
                    free(command);
                    reboot = TRUE;
                } else if (!strcmp(gModel, "gl-x750")) {
                    String command = NULL;
                    command = StrConcat(command, "uci set wireless.default_radio0.ssid='NEDSchool-");
                    command = StrConcat(command, mac5);
                    command = StrConcat(command, "-5G';");
                    
                    command = StrConcat(command, "uci set wireless.default_radio1.ssid='NEDSchool-");
                    command = StrConcat(command, mac5);
                    command = StrConcat(command, "';");
                    
                    command = StrConcat(command, "uci set wireless.guest5g.ssid='NEDGeneral-");
                    command = StrConcat(command, mac5);
                    command = StrConcat(command, "-5G';");
                    command = StrConcat(command, "uci set wireless.guest5g.disabled='0';");
                    
                    command = StrConcat(command, "uci set wireless.guest2g.ssid='NEDGeneral-");
                    command = StrConcat(command, mac5);
                    command = StrConcat(command, "';");                    
                    command = StrConcat(command, "uci set wireless.guest2g.disabled='0';");
                    
                    command = StrConcat(command, "uci commit wireless");
                    ExecuteCommand(command, NULL);
                    free(command);
                    LogPermanent(gName, "Setting wireless");
                    reboot = TRUE;
                } else {
                    Logger(LOG_ERROR, "Unknown model.");
                }
            }
            
            String sp2 = strstr(sp, "option key");
            if (sp2) {
                String sp3 = strtok(sp2, "'");
                sp3 = strtok(NULL, "'");
                char buf[80];
                sprintf(buf, "Found WifiPassword = '%s'", sp3);
                LogPermanent(gName, buf);
                StrReplace(&sWifiPassword, sp3);
            }
            
            free(sp);
        }
        
        sp = ReadTextFile("/etc/config/glconfig");
        if (sp) {
            if (!strstr(sp, "option password")) {
                String command = NULL;
                command = StrConcat(command, "uci set glconfig.general.password='12345';");
                command = StrConcat(command, "uci commit glconfig;");
                ExecuteCommand(command, NULL);
                free(command);
                command = NULL;
                
                FILE* fp = fopen("/tmp/dopasswd", "w");
                if (fp) {
                    fputs("echo -e \"!Tmiahm1967\\n!Tmiahm1967\" | passwd\n", fp);
                    fclose(fp);
                    command = StrConcat(command, "chmod 4755 /tmp/dopasswd;/tmp/dopasswd"); 
                    String sp;                    
                    ExecuteCommand(command, &sp);
                    free(command);
                }

                LogPermanent(gName, "Setting passwords");
                reboot = TRUE;
            }
            free(sp);
        }
    }

    if (!sTest) {
        // Check for version file
        String sp = ReadTextFile("/root/version");
        if (!sp || strstr(sp, "nednet2bcore")) {
            FILE *fp = fopen("/root/version", "w");
            if (fp) {
                fputs(gVersion, fp);
                fclose(fp);
            }
        }
        if (sp) {
            free(sp);
        }
        
        if (reboot) {
            ExecuteCommand("reboot", NULL);
            return 0;
        }
    }
#endif
    
    /* Initializes random number generator */
    time_t t;
    srand((unsigned)time(&t));
    sSessionId = rand();
    if (sSessionId < 1) {
        sSessionId = sSessionId * -1;
    }
     
    gPostResponse = &PostResponse;
    
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
    Logger(LOG_DEBUG, "Version: '%s'\n", gVersion);
    Logger(LOG_DEBUG, "    MAC: '%s'\n", gMAC);
    Logger(LOG_DEBUG, "  Model: '%s'\n", gModel);
    Logger(LOG_DEBUG, "  Modem: '%s'\n", gModem);
    Logger(LOG_DEBUG, "  Phone: '%s'\n", gPhoneNumber);
    Logger(LOG_DEBUG, "   IMEI: '%s'\n", gIMEI);
    Logger(LOG_DEBUG, "  ICCID: '%s'\n", gICCID);
    Logger(LOG_DEBUG, "CARRIER: '%s'\n", gCarrier);
    Logger(LOG_DEBUG, "    APN: '%s'\n", gAPN);
    Logger(LOG_DEBUG, "   DDNS: '%s'\n", gDDNS);
    Logger(LOG_DEBUG, " SERIAL: '%s'\n", gSerial);
    
    if (!strcmp(gMAC, "")) {
        Logger(LOG_ERROR, "No MAC found.");
        goto errorExit;
    }
    
    if (!sTest) {
        LogPermanent(gName, "Entering main loop");
    }
    Logger(LOG_INFO, "Entering main loop");
    sLastPingResponse = time(NULL);
    for ( ; ; ) {
        MessageHeartbeat();
        sleep(sReportInterval);
        LoggerCheckRotate();
       
        // Check if should reboot as have not gotten a internet response for more than 5 minutes
        Ping();
        if ((time(NULL) - sLastPingResponse) > 300) {
            LogPermanent(gName, "Rebooting as no internet connection");
            ExecuteCommand("reboot", NULL);
            return 0;
        }
    }
    
  errorExit:
	Logger(LOG_INFO, "Done");
    ApiQuit();
	return 0;
}
