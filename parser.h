/**
 * @brief read uart data and parse it
 * @author Michael Burmeister
 * @date February 9, 2020
 * @version 1.0
 */


void parserInit(void);
void sendResponse(char, int);
void sendResponseT(char*);
void receive(char*, int);