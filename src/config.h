/**
 * @file    config.h
 * @author  Eng: Anas Alhawija
 * @brief   Central configuration, global variables, and type definitions.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description This header file centralizes all user-configurable settings, pin definitions,
 *              global variable declarations (extern), and necessary library includes, providing
 *              a single point of control for the project's parameters.
 */





/**
 * @file config.h
 * @brief Configuration settings, global variables, and type definitions for the GSM Gateway.
 */

#ifndef CONFIG_H
#define CONFIG_H

// --- Included Libraries ---
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <WebSocketsServer.h>
#include <algorithm>

// --- Hardware & Serial Configuration ---
#define RX_PIN D2      ///< SoftwareSerial RX pin (connected to SIM TX)
#define TX_PIN D1      ///< SoftwareSerial TX pin (connected to SIM RX)
#define SIM_BAUD 9600  ///< Baud rate for the SIM900 module

// --- Filesystem Configuration ---
#define CONFIG_FILE "/config.json" ///< Path to the configuration file on LittleFS

// --- Network Configuration ---
#define AP_SSID "GSM-Gateway-Config" ///< SSID for the Access Point configuration mode
const long STATUS_UPDATE_INTERVAL = 1800000; ///< Periodic status update interval (30 minutes)

// --- Structs and Enums ---

/**
 * @struct GatewayConfig
 * @brief Stores all user-configurable settings loaded from/saved to the config file.
 */
struct GatewayConfig {
    char wifi_ssid[33] = "";
    char wifi_password[65] = "";
    char ap_password[65] = "";
    char server_host[100] = "";
    int server_port = 0;
    char server_user[50] = "";
    char server_pass[50] = "";
    char sim_pin[10] = "";
};

/**
 * @enum SmsListState
 * @brief States for the asynchronous SMS listing state machine.
 */
enum SmsListState {
    SMS_LIST_IDLE,
    SMS_LIST_RUNNING
};

/**
 * @enum SmsSendState
 * @brief States for the asynchronous SMS sending state machine.
 */
enum SmsSendState {
    SMS_SEND_IDLE,
    SMS_SEND_SETTING_CHARSET,
    SMS_SEND_WAITING_PROMPT,
    SMS_SEND_WAITING_FINAL_OK
};


// --- Global Object Declarations ---
extern SoftwareSerial sim900;
extern AsyncWebServer server;
extern DNSServer dnsServer;
extern WebSocketsServer webSocket;
extern GatewayConfig config;

// --- Global State Variable Declarations ---
extern bool apMode;
extern String currentIP;
extern String simStatus;
extern String signalQuality;
extern String networkOperator;
extern String simPhoneNumber;
extern unsigned long lastStatusUpdate;
extern String simResponseBuffer;
extern bool simRequiresPin;
extern bool simPinOk;
extern String currentSimCharset;

// --- State Machine Variable Declarations ---
extern SmsListState smsListState;
extern unsigned long smsListStartTime;
extern bool smsWaitingForContent;
extern JsonDocument currentSmsJson;

extern SmsSendState smsSendState;
extern unsigned long smsSendStartTime;
extern String smsNumberToSend;
extern String smsMessageToSend;
extern bool smsIsUnicode;

#endif // CONFIG_H