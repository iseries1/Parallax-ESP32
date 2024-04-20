/**
 * @brief process incoming commands
 * @author Michael Burmeister
 * @date February 11, 2019
 * @version 1.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "cmds.h"
#include "parser.h"
#include "status.h"

static const char* TAG = "cmds";

static int Socket = -1;
static char SRBuff[1024];

extern esp_err_t register_uri();
extern esp_err_t handleReply(int , char *, int , int);
extern esp_err_t getVar(int, char *, char *);
extern esp_err_t polling(int);


void cmd_init(void)
{

}

void doNothing(char* parms)
{
    sendResponse('S', ERROR_NONE);
}

void doJoin(char* parms)
{
    wifi_config_t config;
    char* p, *s;

    s = &parms[1];
    p = strchr(s, ',');
    if (p != NULL)
    {
        esp_wifi_get_config(ESP_IF_WIFI_STA, &config);
        *p = 0;
        strcpy((char*)config.sta.ssid, s);
        p++;
        strcpy((char*)config.sta.password, p);
        statusDisconnect();
        esp_wifi_set_config(ESP_IF_WIFI_STA, &config);
        sendResponse('S', ERROR_NONE);
        statusConnect();
        return;
    }

    sendResponse('E', ERROR_INVALID_ARGUMENT);
}

void doSend(char* parms)
{
    char* p, * s;
    int len;
    int i;

    s = &parms[1];
    p = strchr(s, ',');

    if (p == NULL)
    {
        sendResponse('E', ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    p++;
    len = atoi(p);
    if ((len <= 0) || len > 1024)
    {
        sendResponse('E', ERROR_INVALID_SIZE);
        return;
    }
    if (Socket == -1)
    {
        sendResponse('E', ERROR_INVALID_STATE);
        return;
    }

    i = 0;
    while (i < len)
    {
        i += receiveBytes(&SRBuff[i], 1024-i);
    }

    if (Socket == -1)
    {
        sendResponse('E', ERROR_INVALID_STATE);
        return;
    }

    if (write(Socket, SRBuff, i) < 0)
    {
        sendResponse('E', ERROR_SEND_FAILED);
        close(Socket);
        Socket = -1;
        return;
    }

    sendResponse('S', ERROR_NONE);
}

void doRecv(char* parms)
{
    char* p, * s;
    int len;
    struct timeval receiving_timeout;

    s = &parms[1];
    p = strchr(s, ',');
    if (p == NULL)
    {
        sendResponse('E', ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    p++;
    len = atoi(p);
    if (Socket == -1)
    {
        sendResponse('E', ERROR_INVALID_STATE);
        return;
    }

    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout, sizeof(receiving_timeout));

    len = read(Socket, SRBuff, len);

    sendResponse('S', len);

    sendBytes(SRBuff, len);
}

void doConnect(char* parms)
{
    char* p, * s;
    char* url;
    int port;
    struct addrinfo* res;
    struct sockaddr_in sockaddr;
    uint32_t addr; 
    int err;

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    
    s = &parms[1];
    p = strchr(s, ',');
    if (p == NULL)
    {
        sendResponse('E', ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    *p = 0;
    p++;
    url = s;
    s = p;
    port = atoi(s);
    if (port == 0)
    {
        sendResponse('E', ERROR_INVALID_ARGUMENT);
        return;
    }

    if (url[0] <= '9')
    {
        addr = esp_ip4addr_aton(url);
        sockaddr.sin_family = AF_INET;
        sockaddr.sin_addr.s_addr = htonl(addr);
        sockaddr.sin_port = htons(port);
    }
    else
    {
        err = getaddrinfo(url, s, &hints, &res);
        if ((err != 0) || (res == NULL))
        {
            ESP_LOGE(TAG, "DNS lookup failed err=%d", err);
            return;
        }
        sockaddr.sin_family = ((struct sockaddr_in*)res->ai_addr)->sin_family;
        sockaddr.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
        sockaddr.sin_port = htons(port);
        freeaddrinfo(res);
    }

    Socket = socket(sockaddr.sin_family, SOCK_STREAM, 0);
    if (connect(Socket, (const struct sockaddr*)&sockaddr, sizeof(sockaddr)) != 0)
    {
        sendResponse('E', ERROR_CONNECT_FAILED);
        close(Socket);
        Socket = -1;
        return;
    }

    sendResponse('S', Socket);
}

void doClose(char* parms)
{
    int i;

    i = atoi(parms);

    if (Socket == -1)
    {
        sendResponse('E', ERROR_INVALID_STATE);
        return;
    }
    close(Socket);
    Socket = -1;
    sendResponse('S', ERROR_NONE);
}

/* set uri http, tcp, ...*/
void doListen(char *parms)
{
    char *s, *p;

    s = &parms[1];
    p = strchr(s, ',');
    if ((p == NULL) && (*s < MIN_TOKEN))
    {
        sendResponse('E', ERROR_INVALID_ARGUMENT);
        return;
    }

    p++;
    
    ESP_LOGI(TAG, "<%s>", p);

    register_uri(p);

    sendResponse('S', ERROR_NONE);
}

/* handle, code(200), total count, count \r <data> */
void doReply(char *parms)
{

    int handle;
    char code[5];
    int tcount;
    int count;
    char *s, *p;

    tcount = 0;
    count = 0;
    s = &parms[1];
    p = strchr(s, ',');
    if (p == NULL)
    {
        sendResponse('E', ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    *p = 0;
    handle = atoi(s);
    s = p + 1;
    p = strchr(s, ',');
    if (p == NULL)
    {
        sendResponse('E', ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }

    *p = 0;
    strcpy(code, s);
    s = p + 1;
    p = strchr(s, ',');
    if (p == NULL)
    {
        count = atoi(s);
        tcount = count;
        ESP_LOGI(TAG, "CallingReply with:%d", count);
        handleReply(handle, code, tcount, count);
    }

    *p = 0;
    tcount = atoi(s);
    s = p + 1;
    count = atoi(s);
    handleReply(handle, code, tcount, count);

    sendResponse('S', ERROR_NONE);
}


void doArg(char *parms)
{
    char *p, *s;
    char name[128];
    char value[128];
    int handle;

    s = &parms[1];
    p = strchr(s, ',');
    if (p == NULL)
    {
        sendResponse('E', ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    *p = 0;
    handle = atoi(s);

    s = p + 1;
    strcpy(name, s);

    ESP_LOGI(TAG, "name:%s", name);
    getVar(handle, name, value);

    sendResponseT(value);
}

void doPoll(char *parms)
{
    char *s;
    int filter;

    s = &parms[1];
    filter = atoi(s);
    if (filter == 0)
        filter = -1;
    
    polling(filter);

    sendResponse('S', ERROR_NONE);
}

