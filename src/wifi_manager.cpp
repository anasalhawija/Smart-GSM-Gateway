/**
 * @file wifi_manager.cpp
 * @brief Implementation of WiFi, AP, and network management functions.
 */

#include "config.h"
#include "wifi_manager.h"
#include "sim_handler.h" // For updateStatus
#include "web_server.h"  // For notifyClients

/**
 * @brief Initializes WiFi, deciding whether to start in Station or AP mode.
 */
void initializeWifi() {
    if (simPinOk && String(config.wifi_ssid).length() > 0) {
        if (connectWiFi()) {
            startSTAMode();
        } else {
            Serial.println("Initial WiFi connection failed.");
            startAPMode();
        }
    } else {
        if (!simPinOk) Serial.println("Cannot start in STA mode: SIM not ready.");
        if (String(config.wifi_ssid).length() == 0) Serial.println("Cannot start in STA mode: No WiFi config.");
        startAPMode();
    }
}

/**
 * @brief Attempts to connect to the configured WiFi network.
 * @return true on success, false on failure.
 */
bool connectWiFi() {
    if (strlen(config.wifi_ssid) == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    Serial.print("Connecting to WiFi: ");
    Serial.println(config.wifi_ssid);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        currentIP = WiFi.localIP().toString();
        Serial.print("IP Address: ");
        Serial.println(currentIP);
        return true;
    } else {
        Serial.println("\nWiFi Connection Failed.");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
}

/**
 * @brief Starts Access Point (AP) mode for configuration.
 */
void startAPMode() {
    apMode = true;
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));

    bool success = false;
    if (strlen(config.ap_password) >= 8) {
        success = WiFi.softAP(AP_SSID, config.ap_password);
    }
    if (!success) {
        if (strlen(config.ap_password) > 0) Serial.println("AP Password is too short. Starting an open AP.");
        WiFi.softAP(AP_SSID);
    }

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", ip);
    currentIP = WiFi.softAPIP().toString();

    Serial.print("AP Mode Enabled. SSID: ");
    Serial.print(AP_SSID);
    Serial.print(" | IP: ");
    Serial.println(currentIP);
}

/**
 * @brief Starts Station (STA) mode, connecting to a router.
 */
void startSTAMode() {
    apMode = false;
    WiFi.mode(WIFI_STA);
    dnsServer.stop();
    Serial.println("Station (STA) Mode Enabled.");
}

/**
 * @brief Handles recurring tasks in the main loop related to network.
 */
void handleMainLoopTasks() {
    if (apMode) return;

    // Check for WiFi connection loss
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 30000) {
            Serial.println("WiFi connection lost. Attempting to reconnect...");
            connectWiFi();
            lastReconnectAttempt = millis();
        }
    }

    // Handle periodic status updates
    if (millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
        Serial.println("Performing periodic status update...");
        updateStatus(); // from sim_handler
        JsonDocument doc;
        doc["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
        doc["ip_address"] = WiFi.localIP().toString();
        doc["sim_status"] = simStatus;
        doc["signal_quality"] = signalQuality;
        doc["network_operator"] = networkOperator;
        doc["sim_phone_number"] = simPhoneNumber;
        doc["sim_pin_status"] = simRequiresPin ? (simPinOk ? "OK" : "Required") : "Not Required";
        String s;
        serializeJson(doc, s);
        notifyClients("status", s); // from web_server
        lastStatusUpdate = millis();
    }
}