/*
    discovery.c - support for the Parallax WX module discovery protocol

	Copyright (c) 2016 Parallax Inc.
    See the file LICENSE.txt for licensing information.
*/

#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_log.h"
#include "config.h"
#include "discovery.h"
#include "json.h"

#define DISCOVER_PORT   32420

static const char* TAG = "discovery";

static void discovery(void* pvParameters)
{
  char rx_buffer[1024];
  char addr_str[128];
  uint8_t Mac[6];
  int addr_family;
  int ip_protocol;
  int err;
  int len;

  while (1)
  {
      struct sockaddr_in dest_addr;

      dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(DISCOVER_PORT);
      addr_family = AF_INET;
      ip_protocol = IPPROTO_IP;


      int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
      if (sock < 0)
      {
          ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
          break;
      }

      err = bind(sock, (struct sockaddr*) & dest_addr, sizeof(dest_addr));
      if (err < 0)
      {
          ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
      }
      ESP_LOGI(TAG, "UDP listening on port %d", DISCOVER_PORT);

      len = 1;
      while (len > 0)
      {
          struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
          socklen_t socklen = sizeof(source_addr);
          len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr*) & source_addr, &socklen);

          // Error occurred during receiving
          if (len < 0)
          {
              ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
              break;
          }
          // Data received
          else
          {
              // Get the sender's ip address as string
              inet_ntoa_r(((struct sockaddr_in*) & source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);

              if (len == 4)
              {
                  memset(rx_buffer, 0, sizeof(rx_buffer));
                  ESP_LOGI(TAG, "Discovered by: %s", addr_str);
                  esp_wifi_get_mac(0, Mac);
                  json_init(rx_buffer);
                  json_putStr("name", flashConfig.module_name);
                  json_putStr("description", "Don't Care");
                  json_putStr("reset pin", itoa(flashConfig.reset_pin, addr_str, 10));
                  json_putStr("rx pullup", "disabled");
                  sprintf(addr_str, "%02x:%02x:%02x:%02x:%02x:%02x", Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
                  json_putStr("mac address", addr_str);
                  strcat(rx_buffer, "\n");
                  len = strlen(rx_buffer);
                  err = sendto(sock, rx_buffer, len, 0, (struct sockaddr*) & source_addr, sizeof(source_addr));
              }
          }
      }
  }
  vTaskDelete(NULL);
}


void initDiscovery(void)
{
  xTaskCreate(discovery, "udp_server", 4096, NULL, 10, NULL);
}
