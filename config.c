/**
 * @brief process config saved item
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cmds.h"
#include "config.h"

#define MCU_RESET_PIN       12
#define LED_CONN_PIN        16
#define LOADER_BAUD_RATE    115200
#define BAUD_RATE           115200
#define ONE_STOP_BIT        1
#define FLASH_VERSION       1

static const char* TAG = "config";

FlashConfig flashConfig;
FlashConfig flashDefault = {
  .version              = FLASH_VERSION,
  .reset_pin            = MCU_RESET_PIN,
  .conn_led_pin         = LED_CONN_PIN,
  .loader_baud_rate     = LOADER_BAUD_RATE,
  .baud_rate            = BAUD_RATE,
  .dbg_baud_rate        = BAUD_RATE,
  .enable               = 0,
  .start                = TKN_START,
  .events               = 0,
  .dbg_enable           = 0,
  .loader               = 0
};

char NVSLabel[] = "parallax";

esp_err_t configSave(void)
{
  nvs_handle_t my_handle;
  esp_err_t err;

  err = nvs_open(NVSLabel, NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_u32(my_handle, "version", flashConfig.version);
  err = nvs_set_i32(my_handle, "loaderbaudrate", flashConfig.loader_baud_rate);
  err = nvs_set_i32(my_handle, "baudrate", flashConfig.baud_rate);
  err = nvs_set_i32(my_handle, "dbgbaudrate", flashConfig.baud_rate);
  err = nvs_set_i8(my_handle, "connledpin", flashConfig.conn_led_pin);
  err = nvs_set_i8(my_handle, "resetpin", flashConfig.reset_pin);
  err = nvs_set_i8(my_handle, "enable", flashConfig.enable);
  err = nvs_set_u8(my_handle, "start", flashConfig.start);
  err = nvs_set_i8(my_handle, "dbgenable", flashConfig.dbg_enable);
  err = nvs_set_i8(my_handle, "loader", flashConfig.loader);
  err = nvs_set_str(my_handle, "modulename", flashConfig.module_name);
  err = nvs_commit(my_handle);
  nvs_close(my_handle);

  return err;
}

esp_err_t configRestore(void)
{
  nvs_handle_t my_handle;
  esp_err_t err;
  size_t sz;

  err = nvs_open(NVSLabel, NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return err;
  }

  err = nvs_get_u32(my_handle, "version", &flashConfig.version);
  if (err != ESP_OK)
  {
    if (!configRestoreDefaults())
      return err;

    return ESP_OK;
  }

  err = nvs_get_i32(my_handle, "loaderbaudrate", &flashConfig.loader_baud_rate);
  err = nvs_get_i32(my_handle, "baudrate", &flashConfig.baud_rate);
  err = nvs_get_i32(my_handle, "dbgbaudrate", &flashConfig.baud_rate);
  err = nvs_get_i8(my_handle, "connledpin", &flashConfig.conn_led_pin);
  err = nvs_get_i8(my_handle, "resetpin", &flashConfig.reset_pin);
  err = nvs_get_i8(my_handle, "enable", &flashConfig.enable);
  err = nvs_get_u8(my_handle, "start", &flashConfig.start);
  err = nvs_get_i8(my_handle, "dbgenable", &flashConfig.dbg_enable);
  err = nvs_get_i8(my_handle, "loader", &flashConfig.loader);
  sz = sizeof(flashConfig.module_name);
  err = nvs_get_str(my_handle, "modulename", flashConfig.module_name, &sz);
  nvs_close(my_handle);

  return err;
}

esp_err_t configRestoreDefaults(void)
{
  uint8_t value[7];
  char Buffer[10];

  memcpy(&flashConfig, &flashDefault, sizeof(FlashConfig));
  esp_efuse_mac_get_default(value);
  sprintf(Buffer, "%02x%02x%02x", value[3], value[4], value[5]);
  strcpy(flashConfig.module_name, "WX32-");
  strcat(flashConfig.module_name, Buffer);

  ESP_LOGI(TAG, "ESP32: %s", flashConfig.module_name);

  return ESP_OK;
}

