/**
 * @brief read uart data and parse it
 * @author Michael Burmeister
 * @date February 9, 2020
 * @version 1.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "parser.h"
#include "config.h"
#include "cmds.h"
#include "httpd.h"
#include "serbridge.h"

#define BUFFSIZE 256

static const char* TAG = "parser";

char Tokens[][10] = {"", "JOIN", "CHECK", "SET", "POLL", "PATH", "SEND", "RECV", "CLOSE", "LISTEN",
                     "ARG", "REPLY", "CONNECT", "APSCAN", "APGET", "FINFO", "FCOUNT", "FRUN", "UDP"};

char inBuffer[1024];
int iHead, iTail;
char outBuffer[1024];

volatile int In, Out;
bool parse;

void sendResponse(char resp, int value)
{
    char Buf[16];
    int len;

    len = sprintf(Buf, "%c=%c,%d\r", TKN_START, resp, value);
    sendBytes(Buf, len);
}

void sendResponseT(char* value)
{
    char Buf[128];
    int len;

    len = sprintf(Buf, "%c=S,%s\r", TKN_START, value);
    sendBytes(Buf, len);
}

int sendBytes(char *Buf, int len)
{
    int i;

    i = uart_write_bytes(UART_NUM_0, Buf, len);
    return i;
}

int receiveBytes(char *buffer, int len)
{
    int i;

    i = uart_read_bytes(UART_NUM_0, buffer, len, 200 / portTICK_PERIOD_MS);

    return i;
}

void sendResponseP(char type, int handle, int id)
{
    char Buf[128];
    int len;

    len = sprintf(Buf, "%c=%c:%d,%d\r", TKN_START, type, handle, id);
    sendBytes(Buf, len);
}

void receive(char* data, int len)
{
    while (In > 0)
        Delay(200);

    memcpy(inBuffer, data, len);
    In = len;
}

void doCmd()
{
    char* parms;

    parms = &outBuffer[1];

    switch (parms[0])
    {
    case 0:
        doNothing(parms);
        break;
    case TKN_JOIN:
        doJoin(parms);
        break;
    case TKN_SEND:
        doSend(parms);
        break;
    case TKN_RECV:
        doRecv(parms);
        break;
    case TKN_CONNECT:
        doConnect(parms);
        break;
    case TKN_CLOSE:
        doClose(parms);
        break;
    case TKN_CHECK:
        doGet(parms);
        break;
    case TKN_SET:
        doSet(parms);
        break;
    case TKN_LISTEN:
        doListen(parms);
        break;
    case TKN_REPLY:
        doReply(parms);
        break;
    case TKN_ARG:
        doArg(parms);
        break;
    case TKN_POLL:
        doPoll(parms);
        break;
    default :
        printf("*Nothing*\n");
    }
}

void parserInit()
{
    int len;
    int c;
    char Token[10];

    memset(inBuffer, 0, sizeof(inBuffer));
    memset(outBuffer, 0, sizeof(outBuffer));

    uart_config_t uart_config =
    {
        .baud_rate = flashConfig.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    c = uart_driver_install(UART_NUM_0, BUFFSIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Configure a buffer for the incoming data
    char data; // = (uint8_t*)malloc(BUFFSIZE);

    In = 0;
    Out = 0;
    parse = false;
    c = -1;

    ESP_LOGI(TAG, "Uart Configured");
    Delay(1000);

    while (true)
    {
        len = receiveBytes(&data, 1);

        if (len > 0)
        {
            if (parse)
            {
                if (data == '\r')
                {
                    doCmd();
                    Out = 0;
                    outBuffer[Out] = 0;
                    parse = false;
                    continue;
                }
            }

            if (c == 0)
            {
                if (data > MIN_TOKEN)
                    c = -1;
            }

            if (c >= 0)
            {
                if (data == ':')
                {
                    data = doTrans(Token);
                    ESP_LOGI(TAG, "Token is:%x", data);
                    c = -1;
                }
                else
                {
                    Token[c++] = data;
                    Token[c] = 0;
                    if (c > 8)
                        c = -1;
                    continue;
                }
            }

            if (data == TKN_START)
            {
                parse = true;
                c = 0;
            }

            if (Out < 1024)
            {
                outBuffer[Out++] = data;
                outBuffer[Out] = 0;
            }
        }

        if ((Out > 0) && (parse == false))
        {
            serbridgeSend(outBuffer, Out);
            Out = 0;
        }

        if (In > 0)
        {
            sendBytes(inBuffer, In);
            In = 0;
        }
    }
}

char doTrans(char *T)
{
    int i;

    for (i=0;i<19;i++)
        if (strcmp(Tokens[i], T) == 0)
            return 0xf0 - i;
    return ' ';
}

