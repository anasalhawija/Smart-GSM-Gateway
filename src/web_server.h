/**
 * @file    web_server.h
 * @author  Eng: Anas Alhawija
 * @brief   Prototypes for web server and WebSocket handling.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description Declares the functions responsible for setting up and managing the
 *              AsyncWebServer, including HTTP routes for the API and static files,
 *              and the WebSocket server for real-time communication.
 */


/**
 * @file web_server.h
 * @brief Function prototypes for the web server and WebSocket handling.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

// Forward declaration to avoid circular dependencies
class AsyncWebServerRequest;

void setupWebServer();
void handleWebServer();
void notifyClients(const String &type, const String &data);
void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t *payload, size_t length);

#endif // WEB_SERVER_H