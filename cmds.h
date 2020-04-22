/**
 * @brief process incoming commands
 * @author Michael Burmeister
 * @date February 11, 2019
 * @version 1.0
 */

enum
{
    TKN_START = 0xFE,

    TKN_INT8 = 0xFD,
    TKN_UINT8 = 0xFC,
    TKN_INT16 = 0xFB,
    TKN_UINT16 = 0xFA,
    TKN_INT32 = 0xF9,
    TKN_UINT32 = 0xF8,

    TKN_HTTP = 0xF7,
    TKN_WS = 0xF6,
    TKN_TCP = 0xF5,
    TKN_STA = 0xF4,
    TKN_AP = 0xF3,
    TKN_STA_AP = 0xF2,

    // gap for more tokens

    TKN_JOIN = 0xEF,
    TKN_CHECK = 0xEE,
    TKN_SET = 0xED,
    TKN_POLL = 0xEC,
    TKN_PATH = 0xEB,
    TKN_SEND = 0xEA,
    TKN_RECV = 0xE9,
    TKN_CLOSE = 0xE8,
    TKN_LISTEN = 0xE7,
    TKN_ARG = 0xE6,
    TKN_REPLY = 0xE5,
    TKN_CONNECT = 0xE4,
    TKN_APSCAN = 0xE3,
    TKN_APGET = 0xE2,
    TKN_FINFO = 0xE1,
    TKN_FCOUNT = 0xE0,
    TKN_FRUN = 0xDF,
    TKN_UDP = 0xDE,

    MIN_TOKEN = 0x80

};

enum
{
    ERROR_NONE = 0,
    ERROR_INVALID_REQUEST = 1,
    ERROR_INVALID_ARGUMENT = 2,
    ERROR_WRONG_ARGUMENT_COUNT = 3,
    ERROR_NO_FREE_LISTENER = 4,
    ERROR_NO_FREE_CONNECTION = 5,
    ERROR_LOOKUP_FAILED = 6,
    ERROR_CONNECT_FAILED = 7,
    ERROR_SEND_FAILED = 8,
    ERROR_INVALID_STATE = 9,
    ERROR_INVALID_SIZE = 10,
    ERROR_DISCONNECTED = 11,
    ERROR_UNIMPLEMENTED = 12,
    ERROR_BUSY = 13,
    ERROR_INTERNAL_ERROR = 14,
    ERROR_INVALID_METHOD = 15
};


void doNothing(char*);
void doJoin(char*);
void doSend(char*);
void doRecv(char*);
void doConnect(char*);
void doClose(char*);