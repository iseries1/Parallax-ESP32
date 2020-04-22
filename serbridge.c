
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_log.h"
#include "serbridge.h"
#include "config.h"
#include "parser.h"


int sIn, sOut;
static const char* TAG = "serbridge";
static int _PORT;
int currentSocket;

static void doTerminal()
{
  int len;
  char rx_buffer[128];
  sIn = 0;
  sOut = 0;

  do
  {
      len = recv(currentSocket, rx_buffer, sizeof(rx_buffer) - 1, 0);
      if (len < 0)
      {
          ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
      }
      else
      {
          if (len == 0)
          {
              ESP_LOGI(TAG, "Connection closed");
          }
          else
          {
              receive(rx_buffer, len);
          }
      }
  } while (len > 0);
}

void serbridgeSend(char* data, int len)
{
    if (currentSocket >= 0)
        send(currentSocket, data, len, 0);
}


static void serbridge(void* pvParameters)
{
  char addr_str[128];
  int addr_family;
  int ip_protocol;
  int err;

  currentSocket = -1;
  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(_PORT);
  addr_family = AF_INET;
  ip_protocol = IPPROTO_IP;
  inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

  int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
  if (listen_sock < 0)
  {
    ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    vTaskDelete(NULL);
    return;
  }

  err = bind(listen_sock, (struct sockaddr*) & dest_addr, sizeof(dest_addr));
  if (err != 0)
  {
      ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
      goto CLEAN_UP;
  }
  ESP_LOGI(TAG, "Socket bound, port %d", _PORT);

  err = listen(listen_sock, 1);
  if (err != 0)
  {
      ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
      goto CLEAN_UP;
  }

  while (true)
  {
      struct sockaddr_in6 source_addr;
      uint addr_len = sizeof(source_addr);
      currentSocket = accept(listen_sock, (struct sockaddr*) & source_addr, &addr_len);
      if (currentSocket < 0)
      {
          ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
          break;
      }
      inet_ntoa_r(((struct sockaddr_in*) & source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);

      ESP_LOGI(TAG, "Connecting to: %s", addr_str);
      doTerminal();

      shutdown(currentSocket, 0);
      close(currentSocket);
      currentSocket = -1;
  }

CLEAN_UP:
  close(listen_sock);
  vTaskDelete(NULL);
}


// Start transparent serial bridge TCP server on specified port (typ. 23)
void serbridgeInit(int port)
{
  _PORT = port;
  xTaskCreate(serbridge, "serialbridge", 2048, NULL, 10, NULL);
}
