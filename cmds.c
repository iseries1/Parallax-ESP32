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

static const char* TAG = "cmds";

static int Socket = -1;
static char SRBuff[1024];

void doNothing(char* parms)
{
    sendResponse('S', ERROR_NONE);
}

void doJoin(char* parms)
{
    wifi_config_t wifi_config;
    char* p, *s;

    s = &parms[1];
    p = strchr(s, ',');
    if (p != NULL)
    {
        *p = 0;
        strcpy((char*)wifi_config.sta.ssid, s);
        p++;
        strcpy((char*)wifi_config.sta.password, p);
        esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
        esp_wifi_disconnect();
        sendResponse('S', ERROR_NONE);
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
       i += uart_read_bytes(UART_NUM_0, (uint8_t*)&SRBuff[i], 1024-i, 20 / portTICK_RATE_MS);
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

    uart_write_bytes(UART_NUM_0, (const char*)SRBuff, len);
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
    }

    sendResponse('S', ERROR_NONE);
}

void doClose(char* parms)
{
//    char* s;

//    s = &parms[1];

    if (Socket == -1)
    {
        sendResponse('E', ERROR_INVALID_STATE);
        return;
    }
    close(Socket);
    Socket = -1;
    sendResponse('S', ERROR_NONE);
}