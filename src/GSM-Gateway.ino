/**
 * @file    GSM-Gateway.ino
 * @author  Eng: Anas Alhawija
 * @brief   Main entry point for the Smart GSM Gateway project.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description This file contains the primary setup() and loop() functions. It initializes
 *              all modules (FileSystem, SIM, WiFi, WebServer) and orchestrates the main
 *              program flow, acting as the core of the application.
 */



/**
 * @file GSM-Gateway.ino
 * @brief Main file for the ESP8266 GSM Gateway project.
 * @details This file contains the primary setup and loop functions. It initializes all modules
 *          (FileSystem, SIM, WiFi, WebServer) and handles the main program flow.
 */

#include "config.h"
#include "file_system.h"
#include "sim_handler.h"
#include "wifi_manager.h"
#include "web_server.h"

// --- Global Variable Definitions ---
// (These are declared as 'extern' in config.h and defined here)
SoftwareSerial sim900(RX_PIN, TX_PIN);
AsyncWebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket = WebSocketsServer(81);
GatewayConfig config;

// State Variables
bool apMode = false;
String currentIP = "0.0.0.0";
String simStatus = "Initializing...";
String signalQuality = "N/A";
String networkOperator = "N/A";
String simPhoneNumber = "N/A";
unsigned long lastStatusUpdate = 0;
String simResponseBuffer = "";
bool simRequiresPin = false;
bool simPinOk = false;
String currentSimCharset = "";

// State Machine Variables
SmsListState smsListState = SMS_LIST_IDLE;
unsigned long smsListStartTime = 0;
bool smsWaitingForContent = false;
JsonDocument currentSmsJson;

SmsSendState smsSendState = SMS_SEND_IDLE;
unsigned long smsSendStartTime = 0;
String smsNumberToSend;
String smsMessageToSend;
bool smsIsUnicode = false;

/**
 * @brief Main setup function, runs once on boot.
 */
void setup()
{
    Serial.begin(115200);
    Serial.println("\nBooting GSM Gateway...");

    sim900.begin(SIM_BAUD);
    Serial.println("Waiting for modem to stabilize (5 seconds)...");
    delay(5000);

    initFileSystem();
    loadConfig();
    initializeSIM();
    initializeWifi();

    setupWebServer();
    server.begin();
    Serial.println("HTTP server started.");

    if (!apMode) {
        webSocket.begin();
        webSocket.onEvent(handleWebSocketMessage);
        Serial.println("WebSocket server started.");
        // Set last update time to force an immediate first update
        lastStatusUpdate = millis() - STATUS_UPDATE_INTERVAL + 5000;
    }
}

/**
 * @brief Main loop function, runs continuously.
 */
void loop()
{
    // Handle web server and DNS requests
    handleWebServer();

    // Handle incoming data from the SIM module and state machines
    handleSimData();

    // Handle WiFi connectivity and periodic status updates
    handleMainLoopTasks();

    // A small delay to yield to other processes
    delay(10);
}