/**
 * @brief build status information
 * @author Michael Burmeister
 * @date February 7, 2020
 * @version 1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "status.h"

#define WIFI_CONNECTED 0x01
#define WIFI_STA       0x02
#define WIFI_AP        0x04
#define WIFI_IP        0x08
#define WIFI_FAIL      0x10

EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const char* TAG = "status";
gpio_config_t io_conf;
static led_strip_handle_t led_strip;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    wifi_event_ap_staconnected_t* event;
    wifi_config_t wifi_config;
    wifi_mode_t mode;

    esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_get_mode(&mode);

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START :
            ESP_LOGI(TAG, "starting connection to SSID %s", wifi_config.sta.ssid);
            if (*wifi_config.sta.ssid != 0)
            {
                statusConnect();
            }
            break;
        case WIFI_EVENT_STA_STOP :
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED);
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA);
            break;
        case WIFI_EVENT_STA_CONNECTED :
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED);
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL || WIFI_STA);
            break;
        case WIFI_EVENT_AP_START :
            xEventGroupSetBits(s_wifi_event_group, WIFI_AP);
            break;
        case WIFI_EVENT_AP_STOP :
            xEventGroupClearBits(s_wifi_event_group, WIFI_AP);
            break;
        case WIFI_EVENT_AP_STACONNECTED :
            event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED :
            event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "station "MACSTR" left, AID=%d", MAC2STR(event->mac), event->aid);
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED);
            break;
        case WIFI_EVENT_STA_DISCONNECTED :
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED);
            if ((s_retry_num < 3) && (*wifi_config.sta.ssid != 0) && (mode != WIFI_MODE_AP))
            {
                ESP_ERROR_CHECK(esp_wifi_connect());
                s_retry_num++;
                ESP_LOGI(TAG, "retrying to connect to the access point");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL);
            }
            else
            {
                ESP_LOGW(TAG, "failed to connect");
                if (mode == WIFI_MODE_STA)
                {
                    ESP_LOGI(TAG, "connect failed, setting APSTA mode");
                    esp_wifi_set_mode(WIFI_MODE_APSTA);
                }
            }
            break;
        default :
            ESP_LOGI(TAG, "WiFi event id: %d", (int)event_id);
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
            ESP_LOGI(TAG, "IP event id: %d", (int)event_id);
        }
    }

    ESP_LOGI(TAG, "Flags: %x", (int)xEventGroupGetBits(s_wifi_event_group));
}

void waittoConnect()
{
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED | WIFI_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);
}

void statusDisconnect()
{
    s_retry_num = 5;
    esp_wifi_disconnect();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void statusConnect()
{
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_STA);
    ESP_LOGI(TAG, "Wifi Connecting...");

    ESP_ERROR_CHECK(esp_wifi_connect());
}

int statusGet()
{
    uint8_t b;

    if (s_retry_num == 5)
        return 0;

    if (s_retry_num > 3)
        return 3;

    b = xEventGroupGetBits(s_wifi_event_group);
    if ((b & WIFI_CONNECTED) != 0)
        return 2;

    return 1;
}

bool statusIsConnecting()
{
    uint8_t b;

    b = xEventGroupGetBits(s_wifi_event_group);
    if ((b & WIFI_STA) != 0)
        return true;
    else
        return false;
}

void statusReset()
{
    gpio_set_level(GPIO_NUM_12, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
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

static void statusLoop3(void* pvParameters)
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
            gpio_set_level(8, 1);
        if (pattern == 2)
            if (t == 40)
                gpio_set_level(8, 1);
        if (pattern == 3)
            if (t == 80)
                gpio_set_level(8, 1);
        if (pattern == 4)
            if (t == 160)
                gpio_set_level(8, 1);

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

static void statusLoop2(void* pvParameters)
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
            led_strip_set_pixel(led_strip, 0, 16, 16, 16);
        if (pattern == 2)
            if (t == 40)
                led_strip_set_pixel(led_strip, 0, 16, 16, 16);
        if (pattern == 3)
            if (t == 80)
                led_strip_set_pixel(led_strip, 0, 16, 16, 16);
        if (pattern == 4)
            if (t == 160)
                led_strip_set_pixel(led_strip, 0, 16, 16, 16);

        led_strip_refresh(led_strip);

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
                led_strip_clear(led_strip);
        }
    }
}

void statusInit()
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
  
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_DEF_OUTPUT); //reset pin
    gpio_set_level(GPIO_NUM_12, 1);

#ifdef CONFIG_STRIP
    led_strip_config_t strip_config = 
    {
        .strip_gpio_num = 8,
        .max_leds = 1,
    };

    led_strip_rmt_config_t rmt_config = 
    {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };

    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);

    xTaskCreate(statusLoop2, "status", 1024, NULL, 10, NULL);
#endif

#ifdef CONFIG_RGB
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_DEF_OUTPUT);
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_DEF_OUTPUT);
    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_DEF_OUTPUT);

    gpio_set_level(18, 1);
    gpio_set_level(17, 1);
    gpio_set_level(16, 1);
    xTaskCreate(statusLoop, "status", 1024, NULL, 10, NULL);
#endif

#ifdef CONFIG_LED
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_DEF_OUTPUT);
    gpio_set_level(8, 1);
    xTaskCreate(statusLoop3, "status", 1024, NULL, 10, NULL);
#endif

}
