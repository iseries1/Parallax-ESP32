/**
* @brief HTTP Web Server
* @author Michael Burmeister
* @date January 27, 2020
* @version 1.0
*/



esp_err_t httpdInit(int port);
esp_err_t logInit(void);
esp_err_t logData(char);
int vlogData(const char*, va_list );

void doGet(char*);
void doSet(char*);
