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
#include "driver/uart.h"
#include "driver/gpio.h"

#include "parser.h"
#include "config.h"
#include "cmds.h"
#include "httpd.h"
#include "serbridge.h"

#define BUFFSIZE 256
char inBuffer[1024];
char outBuffer[1024];

int In, Out;
bool parse;

void sendResponse(char resp, int value)
{
    char Buf[16];
    int len;

    len = sprintf(Buf, "%c=%c,%d\r", TKN_START, resp, value);
    uart_write_bytes(UART_NUM_0, (const char*)Buf, len);
}

void sendResponseT(char* value)
{
    char Buf[128];
    int len;

    len = sprintf(Buf, "%c=S,%s\r", TKN_START, value);
    uart_write_bytes(UART_NUM_0, (const char*)Buf, len);
}

void receive(char* data, int len)
{
    for (int i = 0; i < len; i++)
        inBuffer[In++] = data[i];
    inBuffer[In] = 0;
}

void doCmd()
{
    char* parms;

    parms = outBuffer;

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
    default :
        printf("Nothing\n");
    }
}

void parserInit()
{
    int len;
    int p;

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

    uart_driver_install(UART_NUM_0, BUFFSIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Configure a buffer for the incoming data
    uint8_t* data = (uint8_t*)malloc(BUFFSIZE);

    In = 0;
    Out = 0;
    parse = false;
    while (true)
    {
        len = uart_read_bytes(UART_NUM_0, data, BUFFSIZE, 20 / portTICK_RATE_MS);

        p = 0;
        while (p < len)
        {
            if (data[p] == TKN_START)
            {
                parse = true;
                p++;
                continue;
            }

            if (parse)
            {
                if (data[p] == '\r')
                {
                    doCmd();
                    Out = 0;
                    outBuffer[Out] = 0;
                    parse = false;
                    p++;
                    continue;
                }
            }

            if (Out < 1024)
                outBuffer[Out++] = data[p++];

            outBuffer[Out] = 0;
        }

        if ((Out > 0) && (parse == false))
        {
            serbridgeSend(outBuffer, Out);
            Out = 0;
        }

        if (In > 0)
        {
            uart_write_bytes(UART_NUM_0, (const char*)inBuffer, In);
            In = 0;
        }
    }
}