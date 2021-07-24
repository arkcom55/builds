//*******************************************************************
// File:  nednet2bcommon.h
//-------------------------------------------------------------------
// Date:  2020-07-18
// Copyright (c) 2020 Neducation Management Inc.
//-------------------------------------------------------------------

#define TRUE        (1)
#define FALSE       (0)

#define ERR_OK      (0)
#define ERR_ERROR   (1)

#define LOG_ERROR       (1)
#define LOG_WARN        (2)
#define LOG_INFO        (4)
#define LOG_DEBUG       (8)

#define MODEM_PORT      "/dev/ttyUSB2"

#define DEFAULT_PHONE_NUMBER    "13065551212"
#define DEFAULT_IMEI            "IMEI_IMEI_IMEI"
#define DEFAULT_MAC             "FF:FF:FF:FF:FF:FF"
#define DEFAULT_MODEL           "Test"
#define DEFAULT_ICCID           "ICCID_ICCID_ICCID"

typedef char* String;
typedef long long Int64s;

#define MAX_VALUE_LEN (1024)
typedef struct Dict_s {
    char    key[80];
    char    value[MAX_VALUE_LEN];
} Dict_t;

typedef struct Parameters_s {
    int         numParameters;
    Dict_t*     parameters;
} Parameters_t;

typedef struct Message_s {
    String          accountId;
    String          authorizationId;
    int             sequenceNumber;
    Int64s          timestamp;
    String          MAC;
    String          messageId;
    Parameters_t    parameters;
} Message_t;

typedef struct Response_s {
    int             sequenceNumber;
    Int64s          timestamp;
    String          MAC;
    String          messageId;
    int             status;
    cJSON*          data;
    cJSON*          json;
} Response_t;

#define ELEMENT_STRING  (0)
#define ELEMENT_INTEGER (1)
#define ELEMENT_OBJECT  (2)
#define ELEMENT_ARRAY   (3)

typedef struct Element_s {
    int     type;
    char    key[80];
    char    value[256];
} Element_t;

typedef struct Data_s {
    int             numElements;
    Element_t*      elements;
} Data_t;

typedef struct Lease_s {
    int timestamp;
    String mac;
    String ip;
    String name;
    int school;
    struct Lease_s* next;
} Lease_t;

extern int gLogLevel;

extern String gPhoneNumber;
extern String gIMEI;
extern String gMAC;
extern String gUpTime;
extern String gICCID;
extern String gModel;
extern String gModem;
extern int gRemoteLogEnable;

extern int (*gPostResponse)(Response_t* rp);

void ApiInit(String URL);
void ApiQuit();
String GetVersion();

void Logger(int level, String format, ...);
int ParametersInit(Parameters_t* pp);
int ParametersAdd(Parameters_t* pp, String key, String value);
int ParametersAddInt(Parameters_t* pp, String key, int value);
int ParametersAddLong(Parameters_t* pp, String key, Int64s value);
int ParametersFree(Parameters_t* pp);

int MessageLog(int level, String log);
int MessageCommandResponse(int commandReference, String response);
int ResponseParse(Response_t* rp, String sp);
int ResponseFree(Response_t* rp);
int JSONReadAsInt(cJSON* jp);
Message_t* MessageConstruct(String messageId, Parameters_t* parameters);
int MessagePost(Message_t* mp, int responseFlag);
int MessageLog(int level, String log);
int MacRead();
String ReadTextFile(String filename);
cJSON* JSONGetObject(cJSON* json, String name);
String JSONGetString(cJSON* json, String name);
String StrDuplicate(String inSp);
String StrConcat(String base, String toAdd);
int SimRead();
int ExecuteCommand(String command, String* rpp);
Int64s TimeMsec();
int LoggerInit(String program);
int LoggerCheckRotate();
int StrReplace(String* spp, String newString);
long GetCurrentRSS();
int AddToBoot(String program);
Lease_t* GetLeases();
int SetIpTablesNat(int position, String subcommand);
String ConfigRead();