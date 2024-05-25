/**
 * @brief Parallax-Esp32 module
 * @author Michael Burmeister
 * @date January 25, 2020
 * @version 1.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
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
#include "wifi.h"
#include "serbridge.h"
#include "discovery.h"
#include "httpd.h"
#include "captdns.h"
#include "status.h"
#include "parser.h"

static const char *TAG = "main";

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

// Main application entry point
void app_main(void)
{
  esp_err_t ret;

  // Initialize NVS
  ret = initNVS();

  logInit();

#ifdef CONFIG_LOGGING
  ESP_LOGI(TAG, "Verbose Logging");
#endif

  if ((ret = configRestore()) != ESP_OK)
    configSave();

  ESP_LOGI(TAG, "finished configRestore: %d", ret);

  statusInit();

  //Startup WiFi
  startWiFi();

  serbridgeInit(23);

  esp_vfs_spiffs_conf_t spiff_conf =
   {
    .base_path = "/spiffs",
    .partition_label = "storage",
    .max_files = 5,
    .format_if_mount_failed = false
   };

  Delay(1000);

  ESP_LOGI(TAG, "Starting");

  ret = esp_vfs_spiffs_register(&spiff_conf);
  if (ret != ESP_OK)
    ESP_LOGI(TAG, "Spiffs Failed to Register!");
  else
    ESP_LOGI(TAG, "Spiffs Registered");

  Delay(2000);

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(spiff_conf.partition_label, &total, &used);
  if (ret != ESP_OK)
    ESP_LOGI(TAG, "Failed to get spiffs information %s", esp_err_to_name(ret));
  else
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);

  initDiscovery();
  //cgiPropInit();
  //sscp_init();

  httpdInit(80);

  captdnsInit();

  ESP_LOGI(TAG, "Ready");

  parserInit();

  while (true)
  {
    Delay(5000);
  }
}

