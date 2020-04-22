/**
 * @brief build status information
 * @author Michael Burmeister
 * @date February 7, 2020
 * @version 1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define WIFI_CONNECTED 0x01
#define WIFI_STA       0x02
#define WIFI_AP        0x04
#define WIFI_IP        0x08
#define WIFI_FAIL      0x10

 /* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const char* TAG = "status";
gpio_config_t io_conf;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START :
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_STOP :
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED);
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA);
            break;
        case WIFI_EVENT_STA_CONNECTED :
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED);
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL);
            break;
        case WIFI_EVENT_AP_START :
            xEventGroupSetBits(s_wifi_event_group, WIFI_AP);
            break;
        case WIFI_EVENT_AP_STOP :
            xEventGroupClearBits(s_wifi_event_group, WIFI_AP);
            break;
        case WIFI_EVENT_AP_STACONNECTED :
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED :
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED);
            break;
        case WIFI_EVENT_STA_DISCONNECTED :
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED);
            if (s_retry_num < 3)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL);
            }
            break;
        default :
            ESP_LOGI(TAG, "WiFi event id: %d", event_id);
        }
    }

    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP :
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_IP);
            break;
        case IP_EVENT_STA_LOST_IP :
            xEventGroupClearBits(s_wifi_event_group, WIFI_IP);
            break;
        default :
            ESP_LOGI(TAG, "IP event id: %d", event_id);
        }
    }

    //ESP_LOGI(TAG, "Flags: %x", xEventGroupGetBits(s_wifi_event_group));
}

void statusDisconnect()
{
    s_retry_num = 5;
    esp_wifi_disconnect();
}

void statusConnect()
{
    s_retry_num = 0;
    esp_wifi_connect();
}

int statusGet()
{
    uint8_t b;

    if (s_retry_num == 5)
        return 0;

    if (s_retry_num > 3)
        return 3;

    b = xEventGroupGetBits(s_wifi_event_group);
    if ((b & 0x01) != 0)
        return 2;

    return 1;
}

void statusReset()
{
    gpio_set_level(GPIO_NUM_12, 0);
    vTaskDelay(25 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_NUM_12, 1);
}

static void statusLoop(void* pvParameters)
{
    int t;
    int pattern = 0;
    int bits;
    t = 0;
    while (true)
    {
        vTaskDelay(25 / portTICK_PERIOD_MS);
        t++;
        if (pattern == 1)
            gpio_set_level(18, 1);
        if (pattern == 2)
            if (t == 40)
                gpio_set_level(18, 1);
        if (pattern == 3)
            if (t == 80)
                gpio_set_level(18, 1);
        if (pattern == 4)
            if (t == 160)
                gpio_set_level(18, 1);

        if (t > 160)
        {
            t = 0;
            bits = xEventGroupGetBits(s_wifi_event_group);
            pattern = 0;
            if (bits == 0x0b)
                pattern = 1;
            if (bits == 0x04)
                pattern = 4;
            if (bits == 0x0c)
                pattern = 3;
            if (bits == 0x0f)
                pattern = 2;

            if (pattern != 0)
                gpio_set_level(18, 0);
        }
    }
}

void statusInit()
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_DEF_OUTPUT);
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_DEF_OUTPUT);
    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_DEF_OUTPUT);
    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_DEF_OUTPUT); //reset pin

    gpio_set_level(GPIO_NUM_12, 1);
    gpio_set_level(18, 1);
    gpio_set_level(17, 1);
    gpio_set_level(16, 1);
    xTaskCreate(statusLoop, "status", 1024, NULL, 10, NULL);
}