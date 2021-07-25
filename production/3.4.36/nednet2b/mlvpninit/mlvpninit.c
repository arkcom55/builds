//*******************************************************************
// File:  mlvpninit.c
//-------------------------------------------------------------------
// Date:  2021-06-16
// Copyright (c) 2020 Neducation Management Inc.
//-------------------------------------------------------------------
// 2020-07-29 Initial version
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

String gName = "mlvpninit";
String gVersion = "mlvpninit_1.1-7";

// Defaults
#define API_BASE "https://dnsapi.neducation.ca/api/"
#define API_FILE "mlvpn.php"

#define FREE(rp) {if (rp) {free(rp); rp = NULL;}}

//*******************************************************************
// Template structure
//-------------------------------------------------------------------
typedef struct {
    char    key[80];
    char    value[80];
} TemplateDict_t;

static int sResponse = FALSE;

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
// PostResponse
//-------------------------------------------------------------------
static int PostResponse(Response_t* response) {
    if (!strcmp(response->messageId, "mlvpnParameters")) {
        Logger(LOG_DEBUG, "Received mlvpnParameters response");
        
        sResponse = TRUE;

        int mlvpnIndex = 0;
        String mlvpnServerIP = NULL;
        int mlvpnEnabled = 0;
        int mlvpnFound = 0;

        String wdslSSID = NULL;
        String wdslKey = NULL;
        int wdslEnabled = 0;
        int wdslFound = 0;

        String wiFi1SSID = NULL;
        String wiFi1Key = NULL;
        int wiFi1Enabled = 0;
        int wiFi1Found = 0;

        String wiFi2SSID = NULL;
        String wiFi2Key = NULL;
        int wiFi2Enabled = 0;
        int wiFi2Found = 0;

        for (cJSON* item = response->data; item; item = item->next) {
            Logger(LOG_DEBUG, "Item key '%s', type=%d", item->string, item->type);
            if (!strcmp(item->string, "mlvpnServerIP")) {
                StrReplace(&mlvpnServerIP, item->valuestring);
            } else if (!strcmp(item->string, "mlvpnIndex")) {
                mlvpnIndex = (int)(item->valuedouble);
            } else if (!strcmp(item->string, "mlvpnEnabled")) {
                mlvpnEnabled = (int)(item->valuedouble);
                mlvpnFound = 1;
            } else if (!strcmp(item->string, "wdslSSID")) {
                StrReplace(&wdslSSID, item->valuestring);
            } else if (!strcmp(item->string, "wdslKey")) {
                StrReplace(&wdslKey, item->valuestring);
            } else if (!strcmp(item->string, "wdslEnabled")) {
                wdslEnabled = (int)(item->valuedouble);
                wdslFound = 1;
            } else if (!strcmp(item->string, "wiFi1SSID")) {
                StrReplace(&wiFi1SSID, item->valuestring);
            } else if (!strcmp(item->string, "wiFi1Key")) {
                StrReplace(&wiFi1Key, item->valuestring);
            } else if (!strcmp(item->string, "wiFi1Enabled")) {
                wiFi1Enabled = (int)(item->valuedouble);
                wiFi1Found = 1;
            } else if (!strcmp(item->string, "wiFi2SSID")) {
                StrReplace(&wiFi2SSID, item->valuestring);
            } else if (!strcmp(item->string, "wiFi2Key")) {
                StrReplace(&wiFi2Key, item->valuestring);
            } else if (!strcmp(item->string, "wiFi2Enabled")) {
                wiFi2Enabled = (int)(item->valuedouble);
                wiFi2Found = 1;
            }
        }
        
        // Check for MLVPN
        if (mlvpnFound) {
            if (!mlvpnEnabled) {
                ExecuteCommand("uci set nednet.mlvpn.enabled='0'; uci commit nednet", NULL);
            } else if (mlvpnServerIP && mlvpnIndex) {
                char    cmd[80];    
                
                sprintf(cmd, "uci set nednet.mlvpn.server='%s'", mlvpnServerIP);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set nednet.mlvpn.index='%d'", mlvpnIndex);
                ExecuteCommand(cmd, NULL);
                
                ExecuteCommand("uci set nednet.mlvpn.enabled='1'; uci commit nednet", NULL);
            }            
        }

        if (strstr(gModel, "x750")) {
            // Spitz
            // Check for WiFi1
            if (wiFi1Found) {
                char    cmd[80];    
                
                sprintf(cmd, "uci set wireless.guest5g.ssid='%s-%s-5G'", wiFi1SSID, gMAC5);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest5g.key='%s'", wiFi1Key);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest5g.disabled='%d'", wiFi1Enabled ? 0 : 1);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest2g.ssid='%s-%s'", wiFi1SSID, gMAC5);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest2g.key='%s'", wiFi1Key);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest2g.disabled='%d'", wiFi1Enabled ? 0 : 1);
                ExecuteCommand(cmd, NULL);
                
                ExecuteCommand("uci commit wireless", NULL);
            }
            
            // Check for WiFi2
            if (wiFi2Found) {
                char    cmd[80];    
                
                sprintf(cmd, "uci set wireless.default_radio0.ssid='%s-%s-5G'", wiFi2SSID, gMAC5);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio0.key='%s'", wiFi2Key);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio0.disabled='%d'", wiFi2Enabled ? 0 : 1);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio1.ssid='%s-%s'", wiFi2SSID, gMAC5);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio1.key='%s'", wiFi2Key);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio1.disabled='%d'", wiFi2Enabled ? 0 : 1);
                ExecuteCommand(cmd, NULL);
                
                ExecuteCommand("uci commit wireless", NULL);
            }
        } else {
            // Assume MiFi
            // Check for WiFi1
            if (wiFi1Found) {
                char    cmd[80];    
                
                sprintf(cmd, "uci set wireless.guest.ssid='%s-%s'", wiFi1SSID, gMAC5);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest.key='%s'", wiFi1Key);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.guest.disabled='%d'", wiFi1Enabled ? 0 : 1);
                ExecuteCommand(cmd, NULL);
                
                ExecuteCommand("uci commit wireless", NULL);
            }
            
            // Check for WiFi2
            if (wiFi2Found) {
                char    cmd[80];    
                
                sprintf(cmd, "uci set wireless.default_radio0.ssid='%s-%s'", wiFi2SSID, gMAC5);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio0.key='%s'", wiFi2Key);
                ExecuteCommand(cmd, NULL);
                
                sprintf(cmd, "uci set wireless.default_radio0.disabled='%d'", wiFi2Enabled ? 0 : 1);
                ExecuteCommand(cmd, NULL);
                
                ExecuteCommand("uci commit wireless", NULL);
            }
        }
        
        // Check for WDSL
        if (wdslFound) {
            if (!wdslEnabled) {
                ExecuteCommand("/root/mlvpn/scripts/wdsl 0", NULL);
            } else if (wdslSSID && wdslKey) {
                char    cmd[80];    
                sprintf(cmd, "/root/mlvpn/scripts/wdsl 1 \"%s\" \"%s\"", wdslSSID, wdslKey);
                ExecuteCommand(cmd, NULL);
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
// MessageMlvpnParameters
//-------------------------------------------------------------------
int MessageMlvpnParameters() {
    int             status = ERR_ERROR;
    Message_t*      mp = NULL;

    Logger(LOG_DEBUG, "MessageMlvpnParameters");

    Parameters_t    parameters;
    ParametersInit(&parameters);
    
    mp = MessageConstruct("mlvpnParameters", &parameters);
    if (!mp) {
        goto errorExit;
    }
    
    if (MessagePost(mp, TRUE)) {
        goto errorExit;
    }
    
  okExit:
    status = ERR_OK;
    
  errorExit:
    ParametersFree(&parameters);
    if (mp) {
        free(mp);
    }
    return status;
}

//*******************************************************************
// main
//-------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc > 1) {
        MacRead();
        
        printf("     Version: '%s'\n", gVersion);
        printf("         MAC: '%s'\n", gMAC);
        return 0;
    }
    
    LoggerInit(gName);
    Logger(LOG_INFO, "Version: '%s'", gVersion);    
    Logger(LOG_INFO, "MAC: '%s'", gMAC);
    
    gPostResponse = &PostResponse;
    MacRead();
    ModelRead();

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
    
    if (!strcmp(gMAC, "")) {
        Logger(LOG_ERROR, "No MAC found.");
        goto errorExit;
    }
    
    Logger(LOG_INFO, "Entering main loop");
    sResponse = FALSE;
    while (TRUE) {
        MessageMlvpnParameters();
        sleep(10);
        if (sResponse) {
            Logger(LOG_INFO, "Response received");
            break;
        }
    }

    // Get parameteres from uci
    String rp = NULL;
    
    Logger(LOG_INFO, "Checking MLVPN");
    if (!ExecuteCommand("uci get nednet.mlvpn.enabled", &rp)) {
        if (rp && atoi(rp) == 1) {
            FREE(rp);
            Logger(LOG_INFO, "MLVPN enabled");
            String server = NULL;
            int index = 0;
            if (!ExecuteCommand("uci get nednet.mlvpn.server", &rp)) {
                if (rp) {
                    StrReplace(&server, rp);
                }
            }
            FREE(rp);
            if (!ExecuteCommand("uci get nednet.mlvpn.index", &rp)) {
                if (rp) {
                    index = atoi(rp);
                }
            }
            FREE(rp);
            if (server && index) {
                Logger(LOG_INFO, "MLVPN launching...");
                TemplateDict_t vars[2];
                strcpy(vars[0].key, "^mlvpnIndex^");
                sprintf(vars[0].value, "%d", index); 
                strcpy(vars[1].key, "^mlvpnServerIP^");
                strcpy(vars[1].value, server); 
                UpdateTemplateGeneral("/root/mlvpn/startTemplate.txt", "/root/mlvpn/start", vars, 2);
                ExecuteCommand("chmod 755 /root/mlvpn/start", FALSE);
                UpdateTemplateGeneral("/root/mlvpn/stopTemplate.txt", "/root/mlvpn/stop", vars, 2);
                ExecuteCommand("chmod 755 /root/mlvpn/stop", FALSE);
                ExecuteCommand("cd /root/mlvpn;/root/mlvpn/start &", FALSE);
            }
        }
    }        
    FREE(rp);
    
  errorExit:
	Logger(LOG_INFO, "Done");
    ApiQuit();
	return 0;
}
