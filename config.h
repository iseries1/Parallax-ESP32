#ifndef CONFIG_H
#define CONFIG_H


// Flash configuration settings
typedef struct
{
  uint32_t seq; // flash write sequence number
  uint32_t version;
  int32_t  loader_baud_rate;
  int32_t  baud_rate;
  int32_t  dbg_baud_rate;
  int8_t   conn_led_pin;
  int8_t   reset_pin;
  char     module_name[32+1];
  int8_t   enable;
  uint8_t  start;
  int8_t   events;
  int8_t   dbg_enable;
  int8_t   loader;
} FlashConfig;

extern FlashConfig flashConfig;
extern FlashConfig flashDefault;

esp_err_t configSave(void);
esp_err_t configRestore(void);
esp_err_t configRestoreDefaults(void);

int8_t softap_get_ssid(char *ssid, int size);
int8_t softap_set_ssid(const char *ssid, int size);

#endif

#define Delay(x) vTaskDelay(x / portTICK_PERIOD_MS)
