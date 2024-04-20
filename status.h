/**
 * @brief build status information
 * @author Michael Burmeister
 * @date February 7, 2020
 * @version 1.0
 */


/**
 * @brief start status loop
 */
void statusInit(void);
void statusDisconnect(void);
void statusConnect(void);
bool statusIsConnecting(void);
int statusGet(void);
void statusReset(void);
void waittoConnect(void);
