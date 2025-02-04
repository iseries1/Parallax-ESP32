
#include <stdlib.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "settings.h"
#include "config.h"

static const char* TAG = "settings";
static esp_netif_t *Interface;

void setInterface(esp_netif_t *interface)
{
    Interface = interface;
}

int getVersion(void *data, char *value)
{
    sprintf(value, "%s esp-idf(%d.%d.%d)", CONFIG_VERSION, ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    return 0;
}

int getModuleName(void *data, char *value)
{
    strcpy(value, flashConfig.module_name);
    return 0;
}

int setModuleName(void *data, char *value)
{
    memcpy(flashConfig.module_name, value, sizeof(flashConfig.module_name));
    flashConfig.module_name[sizeof(flashConfig.module_name) - 1] = '\0';
    esp_netif_set_hostname(Interface, flashConfig.module_name);
    return 0;
}

int getWiFiMode(void *data, char *value)
{
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    switch (mode) {
    case WIFI_MODE_STA:
        strcpy(value, "STA");
        break;
    case WIFI_MODE_AP:
        strcpy(value, "AP");
        break;
    case WIFI_MODE_APSTA:
        strcpy(value, "APSTA");
        break;
    default:
        return -1;
    }
    return 0;
}

int setWiFiMode(void *data, char *value)
{
    wifi_mode_t mode;
    wifi_mode_t x;

    if (strcmp(value, "STA") == 0)
        x = WIFI_MODE_STA;
    else if (strcmp(value, "AP") == 0)
        x = WIFI_MODE_AP;
    else if (strcmp(value, "APSTA") == 0)
        x = WIFI_MODE_APSTA;
    else if (isdigit((int)value[0]))
        x = atoi(value);
    else
        return -1;
        
    switch (x)
    {
    case WIFI_MODE_STA:
        ESP_LOGW(TAG, "Entering STA mode");
        break;
    case WIFI_MODE_AP:
        ESP_LOGW(TAG, "Entering AP mode");
        break;
    case WIFI_MODE_APSTA:
        ESP_LOGW(TAG, "Entering APSTA mode");
        break;
    default:
        ESP_LOGW(TAG, "Unknown wi-fi mode: %d", mode);
        return -1;
    }

    esp_wifi_get_mode(&mode);

    if (x != mode)
        esp_wifi_set_mode(x);
    
    return 0;
}

int getWiFiSSID(void *data, char *value)
{
    wifi_config_t config;

    esp_wifi_get_config(ESP_IF_WIFI_STA, &config);
	strcpy(value, (char *)config.sta.ssid);
	return 0;
}

int getIPAddress(void *data, char *value)
{
    esp_netif_t* nf = NULL;
    esp_netif_ip_info_t info;

    int interface = (int)data;

    if (interface == 1)
        nf = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (interface == 2)
        nf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    if (nf == NULL)
    {
        strcpy(value, "0.0.0.0");
        return 0;
    }

    if (esp_netif_get_ip_info(nf, &info) != 0)
        return -1;

    esp_ip4addr_ntoa(&info.ip, value, 16);

    return 0;
}

int setIPAddress(void *data, char *value)
{
    esp_netif_t* nf = NULL;
	char* p;
	char dash = '-';
    esp_netif_ip_info_t info;
    esp_netif_dns_info_t dns;
    esp_ip_addr_t dns1;
    esp_ip_addr_t dns2;
	char* v;

    int interface = (int)data;

	p = strchr(value, dash);
	if (p != NULL)
	{
		v = value;
		*p = 0;
		info.ip.addr = esp_ip4addr_aton(v);
		*p = dash;
		v = p + 1;
		p = strchr(v, dash);
		if (p != NULL)
		{
			*p = 0;
			info.netmask.addr = esp_ip4addr_aton(v);
			*p = dash;
			v = p + 1;
			p = strchr(v, dash);
			if (p != NULL)
			{
				*p = 0;
				info.gw.addr = esp_ip4addr_aton(v);
				*p = dash;
				v = p + 1;
				p = strchr(v, dash);
				if (p != NULL)
				{
					*p = 0;
					dns1.u_addr.ip4.addr = esp_ip4addr_aton(v);
					*p = dash;
					v = p + 1;
					dns2.u_addr.ip4.addr = esp_ip4addr_aton(v);
				}
				else
				{
					dns1.u_addr.ip4.addr = esp_ip4addr_aton(v);
				}
			}
		}

        if (interface == 1)
            nf = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (interface == 2)
            nf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

        esp_netif_dhcpc_stop(nf);
        if (dns1.u_addr.ip4.addr != 0)
        {
            dns.ip = dns1;
            esp_netif_set_dns_info(0, ESP_NETIF_DNS_MAIN, &dns);
        }
        if (dns2.u_addr.ip4.addr != 0)
        {
            dns.ip = dns2;
            esp_netif_set_dns_info(0, ESP_NETIF_DNS_BACKUP, &dns);
        }

        esp_netif_set_ip_info(nf, &info);
		return 0;
	}

    return -1;
}

int getMACAddress(void *data, char *value)
{
    int interface = (int)data;
    uint8_t Mac[6];
    
    if (esp_wifi_get_mac(interface-1, Mac) != 0)
        return -1;
        
    sprintf(value, "%02x:%02x:%02x:%02x:%02x:%02x", Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
        
    return 0;
}

int setMACAddress(void *data, char *value)
{
    return -1;
}

int setBaudrate(void *data, char *value)
{
    flashConfig.baud_rate = atoi(value);
    uart_flush_input(UART_NUM_0);
    uart_set_baudrate(UART_NUM_0, flashConfig.baud_rate);
    return 0;
}

int setDbgBaudrate(void *data, char *value)
{
    flashConfig.dbg_baud_rate = atoi(value);
    uart_flush_input(UART_NUM_1);
    uart_set_baudrate(UART_NUM_1, flashConfig.dbg_baud_rate);
    return 0;
}

int setResetPin(void *data, char *value)
{

    flashConfig.reset_pin = atoi(value);
    gpio_reset_pin(flashConfig.reset_pin);
    gpio_set_direction(flashConfig.reset_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(flashConfig.reset_pin, 1);
    return 0;
}

int intGetHandler(void* data, char* value)
{
    int* pValue = (int*)data;
    sprintf(value, "%d", *pValue);
    return 0;
}

int intSetHandler(void* data, char* value)
{
    int* pValue = (int*)data;
    *pValue = atoi(value);
    return 0;
}

int int8GetHandler(void* data, char* value)
{
    int8_t* pValue = (int8_t*)data;
    sprintf(value, "%d", *pValue);
    return 0;
}

int int8SetHandler(void* data, char* value)
{
    int8_t* pValue = (int8_t*)data;
    *pValue = atoi(value);
    return 0;
}

int setLoaderBaudrate(void *data, char *value)
{
    flashConfig.loader_baud_rate = atoi(value);
    return 0;
}

int uint8GetHandler(void* data, char* value)
{
    uint8_t* pValue = (uint8_t*)data;
    sprintf(value, "%d", *pValue);
    return ESP_OK;
}

int uint8SetHandler(void* data, char* value)
{
    uint8_t* pValue = (uint8_t*)data;
    *pValue = atoi(value);
    return 0;
}

