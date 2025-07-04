/**
 * @file    wifi_manager.h
 * @author  Eng: Anas Alhawija
 * @brief   Prototypes for WiFi connection and mode management.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description Declares the functions for managing the ESP8266's WiFi capabilities,
 *              including switching between Access Point (AP) mode for setup and
 *              Station (STA) mode for normal operation.
 */


/**
 * @file wifi_manager.h
 * @brief Function prototypes for WiFi, AP, and general network management.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

void initializeWifi();
bool connectWiFi();
void startAPMode();
void startSTAMode();
void handleMainLoopTasks();

#endif // WIFI_MANAGER_H