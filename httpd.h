/**
* @brief HTTP Web Server
* @author Michael Burmeister
* @date January 27, 2020
* @version 1.0
*/



esp_err_t httpdInit(int port);
esp_err_t logInit(void);
esp_err_t logData(char);
/**
 * @brief Get argument values
 * @param handle of request
 * @param name of argument
 * @param value of argument or NULL
 * @return esp error value
 */
esp_err_t getVar(int handle, char *name, char *value);

int vlogData(const char*, va_list );

void doGet(char*);
void doSet(char*);

