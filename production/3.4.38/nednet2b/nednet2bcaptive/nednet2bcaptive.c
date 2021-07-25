//*******************************************************************
// File:  nednet2b.c
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

String gName = "nednet2bcaptive";
String gVersion = "nednet2bcaptive_20200807_3.2-11";

#define API_BASE "https://dnsapi.neducation.ca/api/"
#define API_FILE "nedNet2b.php"

//*******************************************************************
// main
//-------------------------------------------------------------------
int main(int argc, char **argv) {
    String ip;
    char buf[256];
    Lease_t* leases;
    Lease_t* lp;
    String mac = NULL;
    
    printf("Version: '%s'\n", gVersion);
    if (argc < 2) {
        printf("No ip specified\n");
        return(0);
    }
    ip = argv[1];

    MacRead();
    
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
    
    Logger(LOG_DEBUG, "Looking for ip '%s'", ip);
    
    if (!strcmp(gMAC, "")) {
        Logger(LOG_ERROR, "nednet2bcaptive: No MAC found.");
        goto errorExit;
    }
    
    Logger(LOG_INFO, "nednet2bcaptive: Processing...");
    leases = GetLeases(TRUE);

    // Find mac
    for (lp = leases; lp; lp = lp->next) {        
        // Check for ip
        if (!strcasecmp(ip, lp->ip)) {
            mac = lp->mac;
            break;
        }
    }

    Logger(LOG_DEBUG, "Looking for mac '%s'", mac ? mac : "");
    if (mac) {
        for (lp = leases; lp; lp = lp->next) {    
            // Check for mac
            if (!strcasecmp(mac, lp->mac)) {
                char buf[128];
                //sprintf(buf, "iptables -t nat -D PREROUTING -p tcp -s %s -d 198.251.90.72 -j DNAT --to-destination 138.197.137.249", lp->ip);
                //Logger(LOG_DEBUG, "Executing '%s'", buf);
                //ExecuteCommand(buf, NULL);
                
                sprintf(buf, "iptables -t nat -D PREROUTING -s %s -j ACCEPT", lp->ip);
                Logger(LOG_DEBUG, "Executing '%s'", buf);
                ExecuteCommand(buf, NULL);
            }
        }
    }
    
    ExecuteCommand("iptables -t nat -D PREROUTING -j GL_SPEC_DMZ", NULL);
    ExecuteCommand("iptables -t nat -D PREROUTING -j GL_SPEC_FORWARDING", NULL);
    
    //sprintf(buf, "-p tcp -s %s -d 198.251.90.72 -j DNAT --to-destination 138.197.137.249", ip);
    //SetIpTablesNat(3, buf);
                
    sprintf(buf, "-s %s -j ACCEPT", ip);
    SetIpTablesNat(4, buf);
                
  errorExit:
	Logger(LOG_INFO, "nednet2bcaptive: Done");
    ApiQuit();
	return 0;
}
