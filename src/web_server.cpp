/**
 * @file web_server.cpp
 * @brief Implementation of the web server (HTTP and WebSockets).
 */

#include "config.h"
#include "web_server.h"
#include "file_system.h" // For saveConfig()
#include "sim_handler.h" // For WebSocket actions like sendSMS, etc.

/**
 * @brief Serves static files (CSS, JS, HTML) from LittleFS.
 * @details Adds cache control headers to reduce browser requests.
 */
static void serveStaticFiles() {
    const char *cacheHeader = "max-age=86400"; // Cache for 1 day
    server.on("/style.css", HTTP_GET, [cacheHeader](AsyncWebServerRequest *r) {
        AsyncWebServerResponse* p = r->beginResponse(LittleFS, "/style.css", "text/css");
        p->addHeader("Cache-Control", cacheHeader);
        r->send(p);
    });
    server.on("/script.js", HTTP_GET, [cacheHeader](AsyncWebServerRequest *r) {
        AsyncWebServerResponse* p = r->beginResponse(LittleFS, "/script.js", "text/javascript");
        p->addHeader("Cache-Control", cacheHeader);
        r->send(p);
    });
    server.on("/lang/en.json", HTTP_GET, [cacheHeader](AsyncWebServerRequest *r) {
        AsyncWebServerResponse* p = r->beginResponse(LittleFS,"/lang/en.json","application/json");
        p->addHeader("Cache-Control", cacheHeader);
        r->send(p);
    });
    server.on("/lang/ar.json", HTTP_GET, [cacheHeader](AsyncWebServerRequest *r) {
        AsyncWebServerResponse* p = r->beginResponse(LittleFS,"/lang/ar.json","application/json");
        p->addHeader("Cache-Control", cacheHeader);
        r->send(p);
    });
    // Add other static files here...
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(LittleFS, "/index.html", "text/html");
    });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (LittleFS.exists("/favicon.ico")) r->send(LittleFS, "/favicon.ico", "image/x-icon");
        else r->send(404);
    });
}

/**
 * @brief Sets up all web server routes and handlers.
 */
void setupWebServer() {
    serveStaticFiles();

    // API endpoint to get the current operating mode
    server.on("/getmode", HTTP_GET, [](AsyncWebServerRequest *r) {
        JsonDocument d;
        d["mode"] = apMode ? "AP" : "STA";
        d["sim_ready"] = simPinOk;
        d["sim_pin_required"] = simRequiresPin && !simPinOk;
        String s;
        serializeJson(d, s);
        r->send(200, "application/json", s);
    });

    // API endpoint to scan for WiFi networks (only in AP mode)
    server.on("/scanwifi", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!apMode) { r->send(403, "application/json", R"({"success":false,"message":"Scan only available in AP mode"})"); return; }
        int n = WiFi.scanNetworks();
        JsonDocument doc;
        if (n > 0) {
            // ... (WiFi scanning logic remains identical)
        } else {
            doc["success"] = false;
            doc["message"] = (n == 0) ? "No networks found" : "Scan Error";
        }
        String buf;
        serializeJson(doc, buf);
        r->send(200, "application/json", buf);
        WiFi.scanDelete();
    });

    // API endpoint to save WiFi credentials and reboot
    server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *r) {
        if (!apMode) { r->send(403); return; }
        if (r->hasParam("ssid", true)) {
            strlcpy(config.wifi_ssid, r->getParam("ssid", true)->value().c_str(), sizeof(config.wifi_ssid));
            strlcpy(config.wifi_password, r->getParam("password", true)->value().c_str(), sizeof(config.wifi_password));
            if (saveConfig()) {
                r->send(200, "application/json", R"({"success":true,"message":"WiFi credentials saved. Rebooting..."})");
                delay(1500);
                ESP.restart();
                return;
            }
        }
        r->send(500, "application/json", R"({"success":false,"message":"Failed to save configuration"})");
    });

    // API endpoint to reboot the device
    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *r) {
        r->send(200, "application/json", R"({"success":true,"message":"Rebooting..."})");
        delay(100);
        ESP.restart();
    });

    // Captive portal handler for AP mode
    server.onNotFound([](AsyncWebServerRequest *r) {
        if (apMode && r->host() != WiFi.softAPIP().toString()) {
            r->redirect("http://" + WiFi.softAPIP().toString());
        } else {
            r->send(404);
        }
    });
}

/**
 * @brief Handles web server and WebSocket tasks in the main loop.
 */
void handleWebServer() {
    if (apMode) {
        dnsServer.processNextRequest();
    } else {
        webSocket.loop();
    }
}

/**
 * @brief Broadcasts a message to all connected WebSocket clients.
 * @param type A string defining the message type (e.g., "status", "sms_item").
 * @param data The payload of the message, either a simple string or a JSON string.
 */
void notifyClients(const String &type, const String &data) {
    JsonDocument doc;
    doc["type"] = type;
    bool isJson = (data.startsWith("{") && data.endsWith("}")) || (data.startsWith("[") && data.endsWith("]"));
    if (isJson) {
        JsonDocument nestedDoc;
        if (deserializeJson(nestedDoc, data) == DeserializationError::Ok) {
            doc["data"] = nestedDoc;
        } else {
            doc["data"] = data; // Fallback to string if parsing fails
        }
    } else {
        doc["data"] = data;
    }
    String s;
    if (serializeJson(doc, s) > 0) {
        webSocket.broadcastTXT(s);
    }
}

/**
 * @brief Handles incoming messages from WebSocket clients.
 * @param num The client number.
 * @param type The type of WebSocket event.
 * @param payload The message payload.
 * @param length The length of the payload.
 */
void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    if (type != WStype_TEXT)
        return;

    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok)
        return;

    const char *act = doc["action"];
    if (!act)
        return;

    Serial.printf("[%u]WS Action:%s\n", num, act);
    if ((strcmp(act, "sendSMS") == 0 || strcmp(act, "sendUSSD") == 0 || strcmp(act, "sendUSSDReply") == 0) && !simPinOk)
    {
        notifyClients("error", "SIM not ready");
        return;
    }

    if (strcmp(act, "sendSMS") == 0)
    {
        if (!doc["number"].isNull() && !doc["message"].isNull())
        {
            sendSMS(doc["number"].as<String>(), doc["message"].as<String>());
        }
    }
    else if (strcmp(act, "sendUSSD") == 0)
    {
        if (!doc["code"].isNull())
        {
            sendUSSD(doc["code"].as<String>());
        }
    }
    else if (strcmp(act, "sendUSSDReply") == 0)
    {
        if (!doc["reply"].isNull())
        {
            sendUSSDReply(doc["reply"].as<String>());
        }
    }
    else if (strcmp(act, "getSMSList") == 0)
    {
        startGetSmsList();
    }
    else if (strcmp(act, "readSMS") == 0)
    {
        if (!doc["index"].isNull())
        {
            readSMS(doc["index"].as<int>());
        }
    }
    else if (strcmp(act, "deleteSMS") == 0)
    {
        if (!doc["index"].isNull())
        {
            deleteSMS(doc["index"].as<int>());
        }
    }

    else if (strcmp(act, "getStatus") == 0)
    {
        updateStatus(); // First, refresh the status variables
        // Then, send the updated status to the client
        JsonDocument sD;
        sD["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
        sD["ip_address"] = WiFi.localIP().toString();
        sD["sim_status"] = simStatus;
        sD["signal_quality"] = signalQuality;
        sD["network_operator"] = networkOperator;
        sD["sim_phone_number"] = simPhoneNumber;
        sD["sim_pin_status"] = simRequiresPin ? (simPinOk ? "OK" : "Required") : "Not Required";
        String sS;
        serializeJson(sD, sS);
        notifyClients("status", sS);
    }
}