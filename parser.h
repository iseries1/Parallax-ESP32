/**
 * @brief read uart data and parse it
 * @author Michael Burmeister
 * @date February 9, 2020
 * @version 1.0
 */

/**
 * @brief Startup command parser
 * 
 */
void parserInit(void);

/**
 * @brief Send Response to command
 * @param Resp character
 * @param value of response
 */
void sendResponse(char, int);

/**
 * @brief Send Response text
 * @param value to send
 */
void sendResponseT(char*);

/**
 * @brief Send Response Poll
 * @param type of request
 * @param handle of request
 * @param id of request
 */
void sendResponseP(char, int, int);

/**
 * @brief Send Bytes to serial port
 * @param buf buffer pointer to data
 * @param len length of data to send
 * @return status number of bytes sent or error
*/
int sendBytes(char *Buf, int len);

/**
 * @brief Receive Serial Bytes
 * @param buffer for data to receive
 * @param len length of data to receive
 * @return status number of bytes or error
*/
int receiveBytes(char *buffer, int len);

/**
 * @brief Receive data from telnet
 * @param data character data
 * @param len length of data
 */
void receive(char*, int);

/**
 * @brief Translate Text to token
 * @param T pointer to text value
 * @return token character
 */
char doTrans(char *);