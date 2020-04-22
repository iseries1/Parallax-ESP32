/**
 * @brief Parallax-Esp32 module
 * @author Michael Burmeister
 * @date January 25, 2020
 * @version 1.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "config.h"
#include "serbridge.h"
#include "discovery.h"
#include "httpd.h"
#include "captdns.h"
#include "status.h"
#include "parser.h"

static const char *TAG = "main";

esp_vfs_spiffs_conf_t spiff_conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true };

esp_err_t initNVS()
{
  esp_err_t e;

  e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    nvs_flash_erase();
    e = nvs_flash_init();
  }
  return e;
}

esp_err_t startWiFi()
{
    esp_netif_t* nf;
    wifi_mode_t mode;
    wifi_config_t wifi_config;

  ESP_ERROR_CHECK(esp_netif_init());

  nf = esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();

  ESP_ERROR_CHECK(esp_netif_set_hostname(nf, flashConfig.module_name));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  strcpy((char*)wifi_config.sta.ssid, CONFIG_ESP_WIFI_SSID);
  strcpy((char*)wifi_config.sta.password, CONFIG_ESP_WIFI_PASSWORD);

  if (esp_wifi_get_mode(&mode) != ESP_OK)
  {
      ESP_LOGI(TAG, "configuration set");
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
      ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  }
  else
  {
      ESP_LOGI(TAG, "retreiving configutation");
      ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
      esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
      ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  }

  ESP_ERROR_CHECK(esp_wifi_start());

  return ESP_OK;
}

// Main application entry point
void app_main(void)
{
  int restoreOk;
  
  vTaskDelay(5000/portTICK_PERIOD_MS);
  ESP_LOGI(TAG, "Starting");

  // Initialize NVS
  ESP_ERROR_CHECK(initNVS());

  if (!(restoreOk = configRestore()))
    configSave();

  ESP_LOGI(TAG, "finished configRestore");

  ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiff_conf));
  ESP_LOGI(TAG, "Spiffs Registered");

  size_t total = 0, used = 0;
  esp_spiffs_info(NULL, &total, &used);
  ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);

  statusInit();

  //Startup WiFi
  startWiFi();

  serbridgeInit(23);
  initDiscovery();
  //cgiPropInit();
  //sscp_init();

  httpdInit(80);

  captdnsInit();

  ESP_LOGI(TAG, "Ready");

  logInit();

  parserInit();

  while (true)
  {
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}
