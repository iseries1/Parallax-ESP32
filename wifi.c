/**
 * @brief Config WiFi
 * @author Michael Burmeister
 * @version 1.0
 * @date April 17, 2024
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "config.h"
#include "status.h"
#include "wifi.h"

static const char *TAG = "wifi";
static wifi_config_t config;
static wifi_config_t config_default;

esp_err_t config_AP()
{
  esp_netif_t *ap;
  
  ESP_LOGI(TAG, "Config AP");

  ap = esp_netif_create_default_wifi_ap();
  ESP_ERROR_CHECK(esp_netif_set_hostname(ap, flashConfig.module_name));

  strcpy((char*)config.ap.ssid, flashConfig.module_name);
  
  ESP_LOGI(TAG, "Setting WiFi Name: %s", flashConfig.module_name);

  config.ap.ssid_len = strlen(flashConfig.module_name);
  config.ap.authmode = WIFI_AUTH_OPEN;
  config.ap.channel = 4;
  config.ap.max_connection = 4;
  memset(config.ap.password, 0, sizeof(config.ap.password));
  config.ap.authmode = WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));

  return ESP_OK;
}

esp_err_t config_STA()
{
  esp_netif_t *sta;

  ESP_LOGI(TAG, "Config STA"); 

  esp_wifi_get_config(WIFI_IF_STA, &config);

  sta = esp_netif_create_default_wifi_sta();

  config_default.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  config_default.sta.failure_retry_cnt = 3;
  config_default.sta.threshold.authmode = WIFI_AUTH_OPEN;
  config_default.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  config_default.sta.pmf_cfg.capable = true;
  config_default.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_netif_set_hostname(sta, flashConfig.module_name));

  if (*config.sta.ssid == 0)
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config_default));
  else
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));

  return ESP_OK;
}

esp_err_t startWiFi()
{
  wifi_mode_t mode;

  ESP_LOGI(TAG, "WiFi Init");

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_LOGI(TAG, "Getting Mode");
  if (esp_wifi_get_mode(&mode) != ESP_OK)
  {
    ESP_LOGI(TAG, "Mode Error setting AP");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    config_AP();
    config_STA();
    ESP_LOGI(TAG, "Default WiFi Starting");
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  config_AP();
  config_STA();

  if (mode == WIFI_MODE_AP)
  {
    ESP_LOGI(TAG, "Setting Access Point Config");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
//    config_AP();
  }

  if (mode == WIFI_MODE_STA)
  {
    ESP_LOGI(TAG, "Setting Station Config");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//    config_STA();
  }

//  ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

  ESP_LOGI(TAG, "WiFi Starting");
  ESP_ERROR_CHECK(esp_wifi_start());

  if (mode == WIFI_MODE_STA)
  {
      waittoConnect();
  }

  return ESP_OK;
}
