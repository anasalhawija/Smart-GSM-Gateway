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