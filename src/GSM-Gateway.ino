
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

// --- Configuration ---
#define RX_PIN D2 // GPIO4
#define TX_PIN D1 // GPIO5
#define SIM_BAUD 9600
#define CONFIG_FILE "/config.json"

// AP Mode Settings
const char *AP_SSID = "GSM-Gateway-Config";

// --- Global Variables ---
SoftwareSerial sim900(RX_PIN, TX_PIN);
AsyncWebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket = WebSocketsServer(81);

// Config Struct
struct GatewayConfig
{
    char wifi_ssid[33] = "";
    char wifi_password[65] = "";
    char ap_password[65] = "";
    char server_host[100] = "";
    int server_port = 0;
    char server_user[50] = "";
    char server_pass[50] = "";
    char sim_pin[10] = "";
};
GatewayConfig config;

// State Variables
bool apMode = false;
String currentIP = "0.0.0.0";
String simStatus = "Initializing...";
String signalQuality = "N/A";
String networkOperator = "N/A";
String simPhoneNumber = "N/A";
unsigned long lastStatusUpdate = 0;
const long statusUpdateInterval = 1800000; // 30 mins
String simResponseBuffer = "";
bool simRequiresPin = false;
bool simPinOk = false;

// --- State Machines (Original Names Restored) ---
enum SmsListState
{
    SMS_LIST_IDLE,
    SMS_LIST_RUNNING,

};
SmsListState smsListState = SMS_LIST_IDLE;
unsigned long smsListStartTime = 0;
bool smsWaitingForContent = false;
JsonDocument currentSmsJson;

enum SmsSendState
{
    SMS_SEND_IDLE,
    SMS_SEND_SETTING_CHARSET,
    SMS_SEND_WAITING_PROMPT,
    SMS_SEND_WAITING_FINAL_OK
};
SmsSendState smsSendState = SMS_SEND_IDLE;
unsigned long smsSendStartTime = 0;
String smsNumberToSend;
String smsMessageToSend;
bool smsIsUnicode = false;

// --- Function Prototypes ---
void setupWebServer();
void startAPMode();
bool connectWiFi();
void startSTAMode();
bool loadConfig();
bool saveConfig();
void notifyClients(const String &type, const String &data);
void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
String sendATCommand(const String &cmd, unsigned long timeout, const char *expectedResponsePrefix, bool silent);
void handleSimData();
void sendSMS(const String &number, const String &message);
void sendUSSD(const String &code);
void sendUSSDReply(const String &reply);
void updateStatus();
bool checkSimPin();
void initializeSIM();
void serveStaticFiles();
void readSMS(int index);
void deleteSMS(int index);
void startGetSmsList();
void handleSmsListLine(const String &line);
void handleSmsSendLine(const String &line);
String decodeUcs2(const String &hexStr);
String encodeUcs2(const String &utf8Str);

// --- Arduino Setup ---
void setup()
{
    Serial.begin(115200);
    Serial.println("\nBooting Gateway...");
    sim900.begin(SIM_BAUD);
    Serial.println("Waiting for modem to stabilize (5 seconds)...");
    delay(5000);
    Serial.println("Init LittleFS...");
    if (!LittleFS.begin())
    {
        Serial.println("FS fail! Format...");
        if (LittleFS.format())
            Serial.println("OK.");
        else
        {
            Serial.println("Format failed!");
            while (1)
                delay(1000);
        }
    }
    else
    {
        Serial.println("FS mounted.");
    }
    if (loadConfig())
        Serial.println("Config loaded.");
    else
        Serial.println("No config.");
    initializeSIM();
    if (simPinOk && String(config.wifi_ssid).length() > 0)
    {
        if (connectWiFi())
            startSTAMode();
        else
        {
            Serial.println("WiFi connect fail.");
            startAPMode();
        }
    }
    else
    {
        if (!simPinOk)
            Serial.println("SIM not ready.");
        if (String(config.wifi_ssid).length() == 0)
            Serial.println("No WiFi config.");
        startAPMode();
    }
    setupWebServer();
    server.begin();
    Serial.println("HTTP server OK.");
    if (!apMode)
    {
        webSocket.begin();
        webSocket.onEvent(handleWebSocketMessage);
        Serial.println("WS server OK.");
        lastStatusUpdate = millis() - statusUpdateInterval + 5000;
    }
}

// --- Arduino Loop ---
void loop()
{
    if (apMode)
    {
        dnsServer.processNextRequest();
    }
    else
    {
        webSocket.loop();
        if (WiFi.status() != WL_CONNECTED)
        {
            static unsigned long lastReconnectAttempt = 0;
            if (millis() - lastReconnectAttempt > 30000)
            {
                Serial.println("WiFi connection lost. Attempting to reconnect...");
                connectWiFi();
                lastReconnectAttempt = millis();
            }
        }
    }
    handleSimData();
    if (!apMode && millis() - lastStatusUpdate > statusUpdateInterval)
    {
        Serial.println("Periodic status update...");
        updateStatus();
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
        notifyClients("status", s);
        lastStatusUpdate = millis();
    }
    delay(10);
}

// --- WiFi & Mode Management ---
bool connectWiFi()
{
    if (strlen(config.wifi_ssid) == 0)
        return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    Serial.print("Connect WiFi:");
    Serial.println(config.wifi_ssid);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
    {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi OK!");
        currentIP = WiFi.localIP().toString();
        Serial.print("IP:");
        Serial.println(currentIP);
        return true;
    }
    else
    {
        Serial.println("\nWiFi Fail.");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
}
void startAPMode()
{
    apMode = true;
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    bool sec = false;
    if (strlen(config.ap_password) >= 8)
    {
        sec = WiFi.softAP(AP_SSID, config.ap_password);
    }
    if (!sec)
    {
        if (strlen(config.ap_password) > 0)
            Serial.println("AP Pass short. Open AP.");
        Serial.print("Open AP:");
        Serial.println(AP_SSID);
        WiFi.softAP(AP_SSID);
    }
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", ip);
    currentIP = WiFi.softAPIP().toString();
    Serial.print("AP Mode. SSID:");
    Serial.println(AP_SSID);
    if (sec)
        Serial.println("AP Secured.");
    else
        Serial.println("AP OPEN.");
    Serial.print("AP IP:");
    Serial.println(currentIP);
}
void startSTAMode()
{
    apMode = false;
    WiFi.mode(WIFI_STA);
    dnsServer.stop();
    Serial.println("STA Mode.");
}

// --- Configuration Management ---
bool loadConfig()
{
    if (LittleFS.exists(CONFIG_FILE))
    {
        File f = LittleFS.open(CONFIG_FILE, "r");
        if (!f)
        {
            Serial.println("Fail read cfg");
            return false;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err)
        {
            Serial.print("Fail parse cfg:");
            Serial.println(err.c_str());
            return false;
        }
        strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
        strlcpy(config.wifi_password, doc["wifi_password"] | "", sizeof(config.wifi_password));
        strlcpy(config.ap_password, doc["ap_password"] | "", sizeof(config.ap_password));
        strlcpy(config.server_host, doc["server_host"] | "", sizeof(config.server_host));
        config.server_port = doc["server_port"] | 0;
        strlcpy(config.server_user, doc["server_user"] | "", sizeof(config.server_user));
        strlcpy(config.server_pass, doc["server_pass"] | "", sizeof(config.server_pass));
        strlcpy(config.sim_pin, doc["sim_pin"] | "", sizeof(config.sim_pin));
        return true;
    }
    else
    {
        return false;
    }
}
bool saveConfig()
{
    JsonDocument doc;
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_password"] = config.wifi_password;
    doc["ap_password"] = config.ap_password;
    doc["server_host"] = config.server_host;
    doc["server_port"] = config.server_port;
    doc["server_user"] = config.server_user;
    doc["server_pass"] = config.server_pass;
    doc["sim_pin"] = config.sim_pin;
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f)
    {
        return false;
    }
    size_t bW = serializeJson(doc, f);
    f.close();
    return bW > 0;
}

// --- SIM Initialization & PIN Handling ---
bool checkSimPin()
{
    String r = sendATCommand("AT+CPIN?", 8000, "+CPIN:", true);
    if (r.startsWith("+CPIN: READY"))
    {
        Serial.println("SIM Ready.");
        simRequiresPin = false;
        simPinOk = true;
        return true;
    }
    else if (r.startsWith("+CPIN: SIM PIN"))
    {
        Serial.println("SIM PIN needed.");
        simRequiresPin = true;
        simPinOk = false;
        if (strlen(config.sim_pin) > 0)
        {
            Serial.println("Try saved PIN...");
            String pc = "AT+CPIN=" + String(config.sim_pin);
            r = sendATCommand(pc, 5000, "OK", true);
            if (r.startsWith("OK"))
            {
                Serial.println("PIN OK!");
                simPinOk = true;
                delay(3000);
                r = sendATCommand("AT+CPIN?", 3000, "+CPIN:", true);
                simRequiresPin = false;
                return true;
            }
            else
            {
                Serial.println("PIN Rejected/Err!");
                simPinOk = false;
                return false;
            }
        }
        else
        {
            Serial.println("No PIN saved.");
            return false;
        }
    }
    else if (r.startsWith("+CPIN: SIM PUK"))
    {
        Serial.println("PUK needed! Blocked.");
        simRequiresPin = true;
        simPinOk = false;
        simStatus = "PUK Required";
        return false;
    }
    else if (r.indexOf("SIM not inserted") != -1)
    {
        Serial.println("No SIM.");
        simRequiresPin = false;
        simPinOk = false;
        simStatus = "SIM Not Inserted";
        return false;
    }
    else
    {
        Serial.println("Unk PIN status/Err. Resp:" + r);
        simRequiresPin = false;
        simPinOk = false;
        return false;
    }
}
void initializeSIM()
{
    Serial.println("Init SIM...");
    sendATCommand("AT", 1000, "OK", true);
    sendATCommand("ATE0", 1000, "OK", true);
    sendATCommand("AT+CLIP=1", 1000, "OK", true);
    sendATCommand("AT+CMGF=1", 1000, "OK", true);
    if (!checkSimPin())
        Serial.println("SIM init incomplete. Status:" + simStatus);
    else
        Serial.println("SIM Init Ready.");
}

// --- Web Server Setup ---
void serveStaticFiles()
{
    const char *cH = "max-age=86400";
    server.on("/style.css", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse* p=r->beginResponse(LittleFS,"/style.css","text/css"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/script.js", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse* p=r->beginResponse(LittleFS,"/script.js","text/javascript"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/lang/en.json", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse* p=r->beginResponse(LittleFS,"/lang/en.json","application/json"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/lang/ar.json", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse* p=r->beginResponse(LittleFS,"/lang/ar.json","application/json"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r)
              { r->send(LittleFS, "/index.html", "text/html"); });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *r)
              { if(LittleFS.exists("/favicon.ico")) r->send(LittleFS,"/favicon.ico","image/x-icon"); else r->send(404); });
}
void setupWebServer()
{
    serveStaticFiles();
    server.on("/getmode", HTTP_GET, [](AsyncWebServerRequest *r)
              {
        JsonDocument d;
        d["mode"] = apMode ? "AP" : "STA";
        d["sim_ready"] = simPinOk;
        d["sim_pin_required"] = simRequiresPin && !simPinOk;
        String s; serializeJson(d, s);
        r->send(200, "application/json", s); });
    server.on("/scanwifi", HTTP_GET, [](AsyncWebServerRequest *r)
              {
        if (!apMode) { r->send(403, "application/json", R"({"success":false,"message":"Scan AP only"})"); return; }
        int n = WiFi.scanNetworks();
        JsonDocument doc;
        if (n > 0) {
            doc["success"] = true;
            JsonArray netsA = doc["networks"].to<JsonArray>();
            struct WifiNetwork { String s; int32_t r; uint8_t e; };
            WifiNetwork *nets = new WifiNetwork[n];
            for (int i = 0; i < n; ++i) { nets[i] = {WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i)}; }
            std::sort(nets, nets + n, [](const WifiNetwork &a, const WifiNetwork &b) { return a.r > b.r; });
            for (int i = 0; i < n; ++i) {
                JsonObject net = netsA.add<JsonObject>();
                net["ssid"] = nets[i].s; net["rssi"] = nets[i].r; net["secure"] = (nets[i].e != ENC_TYPE_NONE);
            }
            delete[] nets;
        } else { doc["success"] = false; doc["message"] = (n == 0) ? "No nets" : "Scan Err"; }
        String buf; serializeJson(doc, buf);
        r->send(200, "application/json", buf);
        WiFi.scanDelete(); });
    server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *r)
              {
        if (!apMode) { r->send(403); return; }
        if (r->hasParam("ssid", true)) {
            strlcpy(config.wifi_ssid, r->getParam("ssid", true)->value().c_str(), sizeof(config.wifi_ssid));
            strlcpy(config.wifi_password, r->getParam("password", true)->value().c_str(), sizeof(config.wifi_password));
            if (saveConfig()) {
                r->send(200, "application/json", R"({"success":true,"message":"WiFi saved. Reboot..."})");
                delay(1500); ESP.restart(); return;
            }
        }
        r->send(500, "application/json", R"({"success":false,"message":"Save failed"})"); });
    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *r)
              {
        r->send(200, "application/json", R"({"success":true,"message":"Rebooting..."})");
        delay(100); ESP.restart(); });
    server.onNotFound([](AsyncWebServerRequest *r)
                      {
        if (apMode && r->host() != WiFi.softAPIP().toString()) {
            r->redirect("http://" + WiFi.softAPIP().toString());
        } else {
            r->send(404);
        } });
}

// --- WebSocket Handling ---
void notifyClients(const String &type, const String &data)
{
    JsonDocument doc;
    doc["type"] = type;
    bool isJ = (data.startsWith("{") && data.endsWith("}")) || (data.startsWith("[") && data.endsWith("]"));
    if (isJ)
    {
        JsonDocument nestedDoc;
        if (deserializeJson(nestedDoc, data) == DeserializationError::Ok)
        {
            doc["data"] = nestedDoc;
        }
        else
        {
            doc["data"] = data;
        }
    }
    else
    {
        doc["data"] = data;
    }
    String s;
    if (serializeJson(doc, s) > 0)
    {
        webSocket.broadcastTXT(s);
    }
}

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

// --- SIM900 Communication ---
String sendATCommand(const String &cmd, unsigned long timeout, const char *expectedResponsePrefix, bool silent)
{
    while (sim900.available() > 0)
    {
        sim900.read();
        yield();
    }
    if (!silent)
    {
        Serial.print("SIM TX: ");
        Serial.println(cmd);
    }
    sim900.println(cmd);
    unsigned long startWait = millis();
    String responseBuffer = "";
    String relevantLine = "";
    bool commandFinished = false;
    while (millis() - startWait < timeout)
    {
        while (sim900.available() > 0)
        {
            char c = sim900.read();
            if (isPrintable(c) || c == '\r' || c == '\n')
                responseBuffer += c;
        }
        int lineEndIndex;
        while ((lineEndIndex = responseBuffer.indexOf('\n')) != -1)
        {
            String line = responseBuffer.substring(0, lineEndIndex);
            responseBuffer.remove(0, lineEndIndex + 1);
            line.trim();
            if (line.length() > 0)
            {
                if (expectedResponsePrefix && line.startsWith(expectedResponsePrefix))
                {
                    relevantLine = line;
                    if (strcmp(expectedResponsePrefix, "OK") == 0 || strcmp(expectedResponsePrefix, "ERROR") == 0)
                        commandFinished = true;
                }
                else if (line.startsWith("OK"))
                {
                    if (relevantLine.isEmpty())
                        relevantLine = line;
                    commandFinished = true;
                }
                else if (line.startsWith("ERROR") || line.indexOf("ERROR:") != -1)
                {
                    if (relevantLine.isEmpty())
                        relevantLine = line;
                    commandFinished = true;
                }
            }
            if (commandFinished)
                goto end_wait_buffered;
        }
        if (commandFinished)
            break;
        yield();
    }
end_wait_buffered:
    if (!commandFinished)
    {
        relevantLine = "TIMEOUT";
    }
    return relevantLine;
}

// --- SIM Data Handling ---

void handleSimData()
{
    // Check for timeouts in state machines first
    if (smsListState == SMS_LIST_RUNNING && millis() - smsListStartTime > 20000)
    {
        Serial.println("ERROR: Timed out waiting for SMS list 'OK'.");
        notifyClients("sms_list_finished", "{\"status\":\"timeout\"}");
        smsListState = SMS_LIST_IDLE;
    }
    if (smsSendState != SMS_SEND_IDLE && millis() - smsSendStartTime > 30000)
    {
        Serial.println("ERROR: Timed out while sending SMS.");
        notifyClients("sms_sent", "{\"status\":\"ERROR\",\"message\":\"TIMEOUT\"}");
        smsSendState = SMS_SEND_IDLE;
    }

    // Now, process incoming data from the modem
    while (sim900.available() > 0)
    {
        char c = sim900.read();

        // Special case for SMS prompt - معالجة واحدة فقط للرمز '>'
        if (c == '>' && smsSendState == SMS_SEND_WAITING_PROMPT)
        {
            Serial.println("SIM RX: > (Prompt)");
            smsSendStartTime = millis();

            if (smsIsUnicode)
            {
                // إرسال PDU للنصوص العربية
                String pdu = createPDU(smsNumberToSend, smsMessageToSend);
                sim900.print(pdu);
                Serial.println("INFO: إرسال PDU للنص العربي: " + pdu);
            }
            else
            {
                // إرسال النص العادي للإنجليزية
                sim900.print(smsMessageToSend);
                Serial.println("INFO: إرسال النص الإنجليزي: " + smsMessageToSend);
            }

            delay(100);
            sim900.write(26); // Ctrl+Z
            Serial.println("INFO: تم إرسال محتوى الرسالة. انتظار التأكيد النهائي.");
            smsSendState = SMS_SEND_WAITING_FINAL_OK;
            return; // الخروج فوراً لتجنب معالجة '>' كجزء من سطر
        }

        // Process full lines ending with newline
        if (c == '\n')
        {
            simResponseBuffer.trim();
            if (simResponseBuffer.length() > 0)
            {
                // --- INTELLIGENT DISPATCHER LOGIC ---
                // First, check if the line is a URC (Unsolicited Result Code).
                bool isUrc = simResponseBuffer.startsWith("+CMTI:") ||
                             simResponseBuffer.startsWith("+CUSD:") ||
                             simResponseBuffer.startsWith("RING") ||
                             simResponseBuffer.startsWith("+CLIP:") ||
                             simResponseBuffer.startsWith("NO CARRIER");

                if (isUrc)
                {
                    Serial.print("URC RX: ");
                    Serial.println(simResponseBuffer);
                    JsonDocument dataDoc;

                    if (simResponseBuffer.startsWith("+CMTI:"))
                    {
                        int c1 = simResponseBuffer.indexOf(',');
                        if (c1 != -1)
                        {
                            int i = simResponseBuffer.substring(c1 + 1).toInt();
                            if (i > 0)
                            {
                                dataDoc.clear();
                                dataDoc["index"] = i;
                                String s;
                                serializeJson(dataDoc, s);
                                notifyClients("sms_received_indication", s);
                            }
                        }
                    }
                    else if (simResponseBuffer.startsWith("+CUSD:"))
                    {
                        String raw = simResponseBuffer;
                        int colonPos = raw.indexOf(':');
                        int firstComma = raw.indexOf(',', colonPos + 1);
                        String ussdMsg = "";
                        int responseType = -1;
                        int dcs = -1;

                        if (colonPos != -1)
                        {
                            String typeStr = (firstComma != -1) ? raw.substring(colonPos + 1, firstComma) : raw.substring(colonPos + 1);
                            typeStr.trim();
                            if (typeStr.length())
                                responseType = typeStr.toInt();

                            if (firstComma != -1)
                            {
                                int quoteStart = raw.indexOf('"', firstComma);
                                int quoteEnd = (quoteStart != -1) ? raw.indexOf('"', quoteStart + 1) : -1;
                                if (quoteStart != -1 && quoteEnd != -1)
                                {
                                    ussdMsg = raw.substring(quoteStart + 1, quoteEnd);
                                }
                                else
                                {
                                    int lastComma = raw.lastIndexOf(',');
                                    ussdMsg = (lastComma > firstComma) ? raw.substring(firstComma + 1, lastComma) : raw.substring(firstComma + 1);
                                }
                                ussdMsg.trim();

                                int lastComma = raw.lastIndexOf(',');
                                if (lastComma > firstComma)
                                {
                                    String dcsStr = raw.substring(lastComma + 1);
                                    dcsStr.trim();
                                    if (dcsStr.length())
                                        dcs = dcsStr.toInt();
                                }
                            }
                        }

                        String decoded = decodeUcs2(ussdMsg);
                        if (decoded != ussdMsg)
                        {
                            ussdMsg = decoded;
                        }

                        dataDoc.clear();
                        dataDoc["type"] = responseType;
                        dataDoc["message"] = ussdMsg;
                        if (dcs != -1)
                            dataDoc["dcs"] = dcs;
                        String out;
                        serializeJson(dataDoc, out);
                        notifyClients("ussd_response", out);
                    }
                    else if (simResponseBuffer.startsWith("RING"))
                    {
                        notifyClients("call_incoming", "RING");
                    }
                    else if (simResponseBuffer.startsWith("NO CARRIER"))
                    {
                        notifyClients("call_status", "NO CARRIER");
                    }
                    else if (simResponseBuffer.startsWith("+CLIP:"))
                    {
                        int q1 = simResponseBuffer.indexOf('"');
                        int q2 = simResponseBuffer.indexOf('"', q1 + 1);
                        if (q1 != -1 && q2 != -1)
                        {
                            String cid = simResponseBuffer.substring(q1 + 1, q2);
                            dataDoc.clear();
                            dataDoc["caller_id"] = cid;
                            String s;
                            serializeJson(dataDoc, s);
                            notifyClients("caller_id", s);
                        }
                    }
                }
                // If it's NOT a URC, then it must be a response to a command
                else if (smsListState == SMS_LIST_RUNNING)
                {
                    handleSmsListLine(simResponseBuffer);
                }
                else if (smsSendState != SMS_SEND_IDLE)
                {
                    handleSmsSendLine(simResponseBuffer);
                }
                else
                {
                    // It's a normal, non-URC response
                    Serial.print("GENERIC RX: ");
                    Serial.println(simResponseBuffer);
                }
            }
            simResponseBuffer = ""; // Reset buffer for the next line
        }
        else if (c != '\r')
        {
            simResponseBuffer += c;
        }
    }
}

// --- SIM Actions ---

void sendSMS(const String &number, const String &message)
{
    if (smsListState != SMS_LIST_IDLE || smsSendState != SMS_SEND_IDLE)
    {
        notifyClients("error", "النظام مشغول، يرجى المحاولة مرة أخرى.");
        return;
    }

    if (message.length() == 0)
    {
        notifyClients("error", "رسالة فارغة");
        return;
    }

    if (!simPinOk)
    {
        notifyClients("sms_sent", R"({"status":"ERROR","message":"SIM not ready","ar_message":"الشريحة غير جاهزة"})");
        return;
    }

    smsNumberToSend = number;
    smsMessageToSend = message;
    smsIsUnicode = false;

    // فحص وجود أحرف عربية
    for (unsigned int i = 0; i < message.length(); i++)
    {
        unsigned char c = (unsigned char)message[i];
        if (c > 127)
        {
            smsIsUnicode = true;
            Serial.println("INFO: تم اكتشاف نص عربي - سيتم استخدام PDU mode");
            break;
        }
    }

    // مسح البيانات المعلقة
    while (sim900.available() > 0)
    {
        sim900.read();
        yield();
    }

    smsSendState = SMS_SEND_SETTING_CHARSET;
    smsSendStartTime = millis();

    if (smsIsUnicode)
    {
        Serial.println("INFO: إعداد PDU mode للنص العربي");
        // إعداد PDU mode مع المعاملات الصحيحة
        sim900.println("AT+CMGF=0"); // PDU mode
    }
    else
    {
        Serial.println("INFO: إعداد Text mode للنص الإنجليزي");
        sim900.println("AT+CMGF=1"); // Text mode
    }
}

void sendUSSD(const String &code)
{
    notifyClients("ussd_response", "{\"type\":-1,\"message\":\"Sending USSD...\"}");
    sendATCommand("AT+CSCS=\"GSM\"", 1500, "OK", false);
    delay(100);
    sim900.println("AT+CUSD=1,\"" + code + "\",15");
}
void sendUSSDReply(const String &reply)
{
    sendATCommand("AT+CSCS=\"GSM\"", 1500, "OK", false);
    delay(100);
    sim900.println("AT+CUSD=1,\"" + reply + "\",15");
}
void readSMS(int index)
{
    if (index <= 0)
        return;
    String cmd = "AT+CMGR=" + String(index);
    while (sim900.available())
        sim900.read();
    sim900.println(cmd);
    String header = "", body = "";
    bool headerFound = false;
    unsigned long startTime = millis();
    while (millis() - startTime < 5000)
    {
        if (sim900.available())
        {
            String line = sim900.readStringUntil('\n');
            line.trim();
            if (line.length() == 0)
                continue;
            if (line.startsWith("+CMGR:"))
            {
                headerFound = true;
                header = line;
            }
            else if (headerFound && !line.startsWith("OK"))
            {
                body += line;
            }
            else if (line.startsWith("OK"))
                break;
            else if (line.indexOf("ERROR") != -1)
            {
                headerFound = false;
                break;
            }
        }
        yield();
    }
    if (!headerFound)
    {
        notifyClients("sms_content", "{\"error\":\"Failed to read SMS\"}");
        return;
    }
    JsonDocument doc;
    doc["index"] = index;
    body.trim();                           // CORRECTED: Trim the string first.
    String decodedBody = decodeUcs2(body); // Then pass the trimmed string.
    doc["body"] = decodedBody;
    if (decodedBody != body)
        doc["body_hex"] = body;
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    notifyClients("sms_content", jsonOutput);
}
void deleteSMS(int index)
{
    if (index <= 0)
        return;
    String response = sendATCommand("AT+CMGD=" + String(index), 5000, "OK", false);
    JsonDocument doc;
    doc["index"] = index;
    doc["success"] = response.startsWith("OK");
    if (!doc["success"])
        doc["message"] = response;
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    notifyClients("sms_deleted", jsonOutput);
}

// --- Status Update ---
void updateStatus()
{
    if (!checkSimPin())
    {
        if (simStatus != "PUK Required" && simStatus != "SIM Not Inserted")
            simStatus = "SIM Not Ready";
        signalQuality = "N/A";
        networkOperator = "N/A";
        simPhoneNumber = "N/A";
        return;
    }
    String copsLine = sendATCommand("AT+COPS?", 8000, "+COPS:", true);
    if (copsLine.startsWith("+COPS:"))
    {
        int q1 = copsLine.indexOf('"');
        int q2 = copsLine.indexOf('"', q1 + 1);
        if (q1 != -1 && q2 != -1)
            networkOperator = copsLine.substring(q1 + 1, q2);
    }
    String csqLine = sendATCommand("AT+CSQ", 3000, "+CSQ:", true);
    if (csqLine.startsWith("+CSQ:"))
    {
        int cPos = csqLine.indexOf(',');
        if (cPos != -1)
        {
            int rssi = csqLine.substring(csqLine.indexOf(':') + 2, cPos).toInt();
            if (rssi >= 0 && rssi <= 31)
                signalQuality = String(-113 + (2 * rssi)) + " dBm";
            else
                signalQuality = "N/A";
        }
    }
    simStatus = (copsLine.startsWith("+COPS:")) ? "Registered" : "Not Registered";
}

// --- Helper: Decode UCS-2 Hex to UTF-8 ---
String decodeUcs2(const String &hexStr)
{
    String out = "";
    if (hexStr.length() == 0 || hexStr.length() % 4 != 0)
        return hexStr;
    for (unsigned int i = 0; i < hexStr.length(); i += 4)
    {
        for (int j = 0; j < 4; ++j)
        {
            if (!isxdigit(hexStr.charAt(i + j)))
                return hexStr;
        }
        uint16_t code = (uint16_t)strtol(hexStr.substring(i, i + 4).c_str(), NULL, 16);
        if (code < 0x80)
        {
            out += char(code);
        }
        else if (code < 0x800)
        {
            out += char(0xC0 | (code >> 6));
            out += char(0x80 | (code & 0x3F));
        }
        else
        {
            out += char(0xE0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        }
    }
    return out;
}
// --- Helper: UTF-8 to UCS-2 Hex Encoder ---

String encodeUcs2(const String &utf8Str)
{
    String hexStr = "";
    unsigned int strLength = utf8Str.length();

    for (unsigned int i = 0; i < strLength; i++)
    {
        uint16_t ucs2Char;
        unsigned char c = (unsigned char)utf8Str[i];

        if (c < 0x80)
        {
            ucs2Char = c;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            if (i + 1 < strLength)
            {
                unsigned char c2 = (unsigned char)utf8Str[++i];
                ucs2Char = ((c & 0x1F) << 6) | (c2 & 0x3F);
            }
            else
            {
                ucs2Char = 0x003F;
            }
        }
        else if ((c & 0xF0) == 0xE0)
        {
            if (i + 2 < strLength)
            {
                unsigned char c2 = (unsigned char)utf8Str[++i];
                unsigned char c3 = (unsigned char)utf8Str[++i];
                ucs2Char = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            }
            else
            {
                ucs2Char = 0x003F;
            }
        }
        else
        {
            ucs2Char = 0x003F;
        }

        char hex[5];
        sprintf(hex, "%04X", ucs2Char);
        hexStr += hex;
    }

    Serial.println("DEBUG: UTF-8 '" + utf8Str + "' -> UCS2 '" + hexStr + "'");
    return hexStr;
}
// =========================================================================
// SMS List State Machine Implementation
// =========================================================================
// --- SMS List State Machine Logic ---
void startGetSmsList()
{
    if (smsListState != SMS_LIST_IDLE)
    {
        Serial.println("WARN: getSMSList already running.");
        return;
    }
    Serial.println("INFO: Starting non-blocking SMS list retrieval.");
    while (sim900.available())
        sim900.read();

    smsListState = SMS_LIST_RUNNING;
    smsListStartTime = millis(); // Start the timeout timer
    smsWaitingForContent = false;

    notifyClients("sms_list_started", "{}");
    sim900.println("AT+CMGL=\"ALL\"");
    Serial.println("SIM TX: AT+CMGL=\"ALL\"");
}

void handleSmsListLine(const String &line)
{
    smsListStartTime = millis(); // Reset timeout timer on each relevant line received

    if (line.startsWith("+CMGL:"))
    {
        currentSmsJson.clear();
        int indexStart = line.indexOf(':') + 1;
        int indexEnd = line.indexOf(',', indexStart);
        currentSmsJson["index"] = line.substring(indexStart, indexEnd).toInt();
        int statusStart = line.indexOf('"', indexEnd) + 1;
        int statusEnd = line.indexOf('"', statusStart);
        currentSmsJson["status"] = line.substring(statusStart, statusEnd);
        int senderStart = line.indexOf('"', statusEnd + 1) + 1;
        int senderEnd = line.indexOf('"', senderStart);
        currentSmsJson["sender"] = line.substring(senderStart, senderEnd);
        int tsStart = line.indexOf('"', senderEnd + 1);
        tsStart = line.indexOf('"', tsStart + 1) + 1;
        int tsEnd = line.indexOf('"', tsStart);
        if (tsStart > 0 && tsEnd > -1)
        {
            currentSmsJson["timestamp"] = line.substring(tsStart, tsEnd);
        }
        else
        {
            currentSmsJson["timestamp"] = "";
        }
        smsWaitingForContent = true;
    }
    else if (smsWaitingForContent)
    {
        String body = line;
        String decodedBody = decodeUcs2(body);
        currentSmsJson["body"] = decodedBody;
        if (decodedBody != body)
        {
            currentSmsJson["body_hex"] = body;
        }
        String jsonOutput;
        serializeJson(currentSmsJson, jsonOutput);
        notifyClients("sms_item", jsonOutput);
        smsWaitingForContent = false;
    }
    else if (line.startsWith("OK"))
    {
        Serial.println("INFO: SMS list retrieval finished successfully.");
        notifyClients("sms_list_finished", "{\"status\":\"complete\"}");
        smsListState = SMS_LIST_IDLE; // Reset the state machine
    }
    else if (line.indexOf("ERROR") != -1)
    {
        Serial.println("ERROR: Failed to retrieve SMS list.");
        notifyClients("sms_list_finished", "{\"status\":\"error\"}");
        smsListState = SMS_LIST_IDLE; // Reset the state machine
    }
}


void handleSmsSendLine(const String &line)
{
    Serial.print("SMS Send RX: ");
    Serial.println(line);
    smsSendStartTime = millis();

    switch (smsSendState)
    {
    case SMS_SEND_IDLE:
        Serial.println("WARNING: Received response while SMS send state is IDLE");
        break;

    case SMS_SEND_SETTING_CHARSET:
        if (line.startsWith("OK"))
        {
            smsSendState = SMS_SEND_WAITING_PROMPT;
            smsSendStartTime = millis();

            if (smsIsUnicode)
            {
                // للنصوص العربية: إرسال PDU
                String pdu = createPDU(smsNumberToSend, smsMessageToSend);
                
                // الحل الصحيح: حساب طول PDU بدون SMSC (أول بايتين)
                int pduLengthWithoutSMSC = (pdu.length() - 2) / 2; // طرح "00" الأولى وقسمة على 2

                sim900.print("AT+CMGS=");
                sim900.println(pduLengthWithoutSMSC);
                Serial.println("SIM TX: AT+CMGS=" + String(pduLengthWithoutSMSC));
                Serial.println("INFO: انتظار رمز PDU للنص العربي - PDU Length: " + String(pduLengthWithoutSMSC));
            }
            else
            {
                // للنصوص الإنجليزية: إرسال عادي
                sim900.print("AT+CMGS=\"");
                sim900.print(smsNumberToSend);
                sim900.println("\"");
                Serial.println("SIM TX: AT+CMGS=\"" + smsNumberToSend + "\"");
                Serial.println("INFO: انتظار رمز الرسالة الإنجليزية '>'");
            }
        }
        else if (line.indexOf("ERROR") != -1)
        {
            Serial.println("ERROR: فشل في إعداد وضع الإرسال");
            notifyClients("sms_sent", R"({"status":"ERROR","message":"Failed to set SMS mode","ar_message":"فشل في إعداد وضع الإرسال"})");
            smsSendState = SMS_SEND_IDLE;
        }
        break;

    case SMS_SEND_WAITING_PROMPT:
        if (line.indexOf("ERROR") != -1)
        {
            if (smsIsUnicode)
            {
                Serial.println("ERROR: فشل في بدء إرسال الرسالة العربية - خطأ في طول PDU أو الرقم");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"Arabic PDU length error or invalid number","ar_message":"خطأ في طول PDU العربي أو رقم غير صالح"})");
            }
            else
            {
                Serial.println("ERROR: فشل في بدء إرسال الرسالة الإنجليزية");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"Failed to send English SMS","ar_message":"فشل في إرسال الرسالة الإنجليزية"})");
            }
            smsSendState = SMS_SEND_IDLE;
        }
        break;

    case SMS_SEND_WAITING_FINAL_OK:
        if (line.startsWith("+CMGS:"))
        {
            return;
        }
        else if (line.startsWith("OK"))
        {
            if (smsIsUnicode)
            {
                Serial.println("INFO: تم إرسال الرسالة العربية بنجاح!");
                notifyClients("sms_sent", R"({"status":"OK","message":"Arabic SMS sent successfully","ar_message":"تم إرسال الرسالة العربية بنجاح"})");
            }
            else
            {
                Serial.println("INFO: تم إرسال الرسالة الإنجليزية بنجاح!");
                notifyClients("sms_sent", R"({"status":"OK","message":"English SMS sent successfully","ar_message":"تم إرسال الرسالة الإنجليزية بنجاح"})");
            }
            smsSendState = SMS_SEND_IDLE;
        }
        else if (line.indexOf("ERROR") != -1)
        {
            if (smsIsUnicode)
            {
                Serial.println("ERROR: فشل في إرسال الرسالة العربية - خطأ في الشبكة أو PDU");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"Arabic SMS network error or PDU format error","ar_message":"خطأ في الشبكة أو تنسيق PDU العربي"})");
            }
            else
            {
                Serial.println("ERROR: فشل في إرسال الرسالة الإنجليزية");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"English SMS failed","ar_message":"فشل في إرسال الرسالة الإنجليزية"})");
            }
            smsSendState = SMS_SEND_IDLE;
        }
        break;
    }
}
//  createPDU
String createPDU(const String &number, const String &message)
{
    String pdu = "00";          // 1- SMSC length = 0 (use default SMSC)
    pdu += "11";                // 2- TP-Mti=01 (SUBMIT) + VPF=10 (Relative)
    pdu += "00";                // 3- TP-MR (Message Reference = 0)

    // 4-A LEN (in digits)
    String msisdn = number.startsWith("+") ? number.substring(1) : number;
    char lenHex[3];
    snprintf(lenHex, sizeof(lenHex), "%02X", (uint8_t)msisdn.length());
    pdu += lenHex;

    // 4-B TON/NPI
    pdu += (number.startsWith("+") ? "91" : "81");

    // 4-C digits BCD swap
    size_t n = msisdn.length();
    for (size_t i = 0; i < n; i += 2)
        pdu += (i + 1 < n)
                 ? String() + msisdn.charAt(i + 1) + msisdn.charAt(i)
                 : String("F") + msisdn.charAt(i);

    pdu += "00";                // TP-PID
    pdu += "08";                // TP-DCS → UCS2
    pdu += "AA";                // VP - ~24 hours

    // User-Data (UCS2)
    String ud = encodeUcs2(message);
    uint8_t udBytes = ud.length() / 2;

    char udl[3];
    snprintf(udl, sizeof(udl), "%02X", udBytes);
    pdu += udl;
    pdu += ud;

    return pdu;
}