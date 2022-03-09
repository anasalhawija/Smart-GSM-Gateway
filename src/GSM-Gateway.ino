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

// --- Function Prototypes ---
void setupWebServer();
void startAPMode();
bool connectWiFi();
void startSTAMode();
bool loadConfig();
bool saveConfig();
void notifyClients(const String &type, const String &data);
void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
String sendATCommand(const String &cmd, unsigned long timeout, const char *expectedResponsePrefix = nullptr, bool silent = false);
void handleSimData();
void sendSMS(const String &number, const String &message);
void sendUSSD(const String &code);
void sendUSSDReply(const String &reply);
void updateStatus();
bool checkSimPin();
void initializeSIM();
void serveStaticFiles();

// --- Arduino Setup ---
void setup()
{
    Serial.begin(115200);
    Serial.println("\nBooting Gateway...");
    sim900.begin(SIM_BAUD);
    delay(1000);
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
        Serial.println("FS mounted.");
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
        dnsServer.processNextRequest();
    else
    {
        webSocket.loop();
        if (WiFi.status() != WL_CONNECTED)
        {
            static unsigned long lastRec = 0;
            if (millis() - lastRec > 30000)
            {
                Serial.println("WiFi lost.");
                connectWiFi();
                lastRec = millis();
            }
        }
    }
    handleSimData();
    if (!apMode && simPinOk && millis() - lastStatusUpdate > statusUpdateInterval)
    {
        Serial.println("Periodic status update...");
        updateStatus();
        StaticJsonDocument<384> doc;
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
        StaticJsonDocument<768> doc;
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
        Serial.println("Config loaded.");
        return true;
    }
    else
    {
        Serial.println("No cfg file.");
        return false;
    }
}
bool saveConfig()
{
    StaticJsonDocument<768> doc;
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
        Serial.println("Fail open cfg write");
        return false;
    }
    size_t bW = serializeJson(doc, f);
    f.close();
    if (bW == 0)
    {
        Serial.println("Fail write cfg");
        return false;
    }
    Serial.printf("Cfg saved(%d B).\n", bW);
    return true;
}

// --- SIM Initialization & PIN Handling ---
bool checkSimPin()
{
    String r = sendATCommand("AT+CPIN?", 5000, "+CPIN:", true);
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
                if (r.startsWith("+CPIN: READY"))
                {
                    Serial.println("SIM confirmed Ready.");
                    simRequiresPin = false;
                    return true;
                }
                else
                {
                    Serial.println("Warn: SIM !Ready after PIN OK?");
                    simPinOk = true;
                    simRequiresPin = false;
                    return true;
                }
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
              { AsyncWebServerResponse*p=r->beginResponse(LittleFS,"/style.css","text/css"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/script.js", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse*p=r->beginResponse(LittleFS,"/script.js","text/javascript"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/lang/en.json", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse*p=r->beginResponse(LittleFS,"/lang/en.json","application/json"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/lang/ar.json", HTTP_GET, [cH](AsyncWebServerRequest *r)
              { AsyncWebServerResponse*p=r->beginResponse(LittleFS,"/lang/ar.json","application/json"); p->addHeader("Cache-Control",cH); r->send(p); });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r)
              { r->send(LittleFS, "/index.html", "text/html"); });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *r)
              {if(LittleFS.exists("/favicon.ico"))r->send(LittleFS,"/favicon.ico","image/x-icon"); else r->send(404); });
}
void setupWebServer()
{
    serveStaticFiles();
    server.on("/getmode", HTTP_GET, [](AsyncWebServerRequest *r)
              {AsyncResponseStream*p=r->beginResponseStream("application/json"); StaticJsonDocument<128> d; d["mode"]=apMode?"AP":"STA"; d["sim_ready"]=simPinOk; d["sim_pin_required"]=simRequiresPin&&!simPinOk; serializeJson(d,*p); r->send(p); });
    server.on("/scanwifi", HTTP_GET, [](AsyncWebServerRequest *r)
              {if(!apMode){r->send(403,"application/json",R"({"success":false,"message":"Scan AP only"})");return;} Serial.println("Scan WiFi..."); WiFi.mode(WIFI_AP); delay(100); int n=WiFi.scanNetworks(); Serial.printf("%d nets.\n",n); StaticJsonDocument<1536> doc; if(n>0){doc["success"]=true; JsonArray netsA=doc.createNestedArray("networks"); struct WifiNetwork{String s;int32_t r;uint8_t e;}; WifiNetwork*nets=new WifiNetwork[n]; if(!nets){Serial.println("Mem fail!");doc["success"]=false;doc["message"]="Mem Err"; n=0;} else{for(int i=0;i<n;++i)nets[i]={WiFi.SSID(i),WiFi.RSSI(i),WiFi.encryptionType(i)}; std::sort(nets,nets+n,[](const WifiNetwork&a,const WifiNetwork&b){return a.r>b.r;}); for(int i=0;i<n;++i){JsonObject net=netsA.add<JsonObject>(); net["ssid"]=nets[i].s; net["rssi"]=nets[i].r; net["secure"]=(nets[i].e!=ENC_TYPE_NONE);} delete[] nets;}} else{doc["success"]=false;doc["message"]=(n==0)?"No nets":"Scan Err";} String buf; serializeJson(doc,buf); r->send(200,"application/json",buf); WiFi.scanDelete(); Serial.println("Scan done."); });
    server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *r)
              {if(!apMode){r->send(403,"application/json",R"({"success":false,"message":"Save WiFi AP only"})");return;} bool ok=false; String msg="Fail save WiFi"; if(r->hasParam("ssid",true)){String s=r->getParam("ssid",true)->value(); String p=r->hasParam("password",true)?r->getParam("password",true)->value():""; strlcpy(config.wifi_ssid,s.c_str(),sizeof(config.wifi_ssid)); strlcpy(config.wifi_password,p.c_str(),sizeof(config.wifi_password)); if(saveConfig()){ok=true; msg="WiFi saved. Reboot..."; Serial.println(msg); AsyncResponseStream*p=r->beginResponseStream("application/json"); StaticJsonDocument<128> d; d["success"]=ok; d["message"]=msg; serializeJson(d,*p); r->send(p); delay(1500); ESP.restart(); return;} else{msg="Err write WiFi cfg."; Serial.println(msg);}} else{msg="'ssid' missing."; Serial.println(msg);} AsyncResponseStream*p=r->beginResponseStream("application/json"); StaticJsonDocument<128> d; d["success"]=ok; d["message"]=msg; serializeJson(d,*p); r->send(p); });
    
server.on("/saveconfig", HTTP_POST, [](AsyncWebServerRequest *r) {
    bool ok = false;
    String msg = "No changes.";
    StaticJsonDocument<256> resp;
    bool changed = false;
    bool recheckPIN = false;

    // ap_password
    if (r->hasParam("ap_password", true)) {
        String v = r->getParam("ap_password", true)->value();
        if (v.length() == 0 || v.length() >= 8) {
            if (strcmp(config.ap_password, v.c_str()) != 0) {
                strlcpy(config.ap_password, v.c_str(), sizeof(config.ap_password));
                changed = true;
            }
        } else {
            msg = "AP Pass short.";
        }
    }

    // server_host
    if (r->hasParam("server_host", true)) {
        String v = r->getParam("server_host", true)->value();
        if (strcmp(config.server_host, v.c_str()) != 0) {
            strlcpy(config.server_host, v.c_str(), sizeof(config.server_host));
            changed = true;
        }
    }

    // server_port
    if (r->hasParam("server_port", true)) {
        int v = r->getParam("server_port", true)->value().toInt();
        if (v >= 0 && v <= 65535 && config.server_port != v) {
            config.server_port = v;
            changed = true;
        } else if (v < 0 || v > 65535) {
            msg = "Port invalid.";
        }
    }

    // server_user
    if (r->hasParam("server_user", true)) {
        String v = r->getParam("server_user", true)->value();
        if (strcmp(config.server_user, v.c_str()) != 0) {
            strlcpy(config.server_user, v.c_str(), sizeof(config.server_user));
            changed = true;
        }
    }

    // server_pass
    if (r->hasParam("server_pass", true)) {
        String v = r->getParam("server_pass", true)->value();
        if (strcmp(config.server_pass, v.c_str()) != 0) {
            strlcpy(config.server_pass, v.c_str(), sizeof(config.server_pass));
            changed = true;
        }
    }

    // sim_pin
    if (r->hasParam("sim_pin", true)) {
        String v = r->getParam("sim_pin", true)->value();
        bool valid = (v.length() == 0) || (v.length() >= 4 && v.length() <= 8);
        for (size_t i = 0; i < v.length(); i++) {
            if (!isDigit(v.charAt(i))) valid = false;
        }
        if (valid) {
            if (strcmp(config.sim_pin, v.c_str()) != 0) {
                strlcpy(config.sim_pin, v.c_str(), sizeof(config.sim_pin));
                changed = true;
            }
        } else {
            msg = "PIN invalid.";
        }
    }

   
    if (changed) {
        if (saveConfig()) {
            ok = true;
            if (simRequiresPin && !simPinOk && strlen(config.sim_pin) > 0) {
                msg = "Cfg saved. Try PIN...";
                recheckPIN = true;
            } else {
                msg = "Cfg saved.";
            }
        } else {
            ok = false;
            msg = "Err write cfg.";
        }
    } else {
        ok = true;
    }

   
    resp["success"] = ok;
    resp["message"] = msg;
    resp["needs_pin_recheck"] = recheckPIN;
    String s;
    serializeJson(resp, s);
    r->send(ok ? 200 : 500, "application/json", s);

    
    if (recheckPIN) {
        delay(500);
        Serial.println("Trigger PIN recheck...");
        if (checkSimPin()) {
            Serial.println("SIM Ready.");
            updateStatus();
            StaticJsonDocument<384> j;
            j["sim_status"] = simStatus;
            String js;
            serializeJson(j, js);
            notifyClients("status", js);
        } else {
            Serial.println("SIM !Ready.");
            StaticJsonDocument<384> j;
            j["sim_status"] = simStatus;
            String js;
            serializeJson(j, js);
            notifyClients("status", js);
        }
    }
});  


server.on("/enterpin", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (!simRequiresPin || simPinOk) {
        r->send(400, "application/json", R"({"success":false,"message":"PIN !req"})");
        return;
    }

    StaticJsonDocument<128> doc;

    if (r->hasParam("pin", true)) {
        String pin = r->getParam("pin", true)->value();
        bool valid = (pin.length() >= 4 && pin.length() <= 8);
        for (size_t i = 0; i < pin.length(); i++) {
            if (!isDigit(pin.charAt(i))) valid = false;
        }

        if (valid) {
            Serial.println("Try PIN...");
            
            String pc = "AT+CPIN=\"" + pin + "\"\r";
            String resp = sendATCommand(pc, 5000, nullptr, false);

            if (resp.startsWith("OK")) {
                Serial.println("PIN OK!");
                simPinOk = true;
                simRequiresPin = false;
                doc["success"] = true;
                doc["message"] = "PIN OK.";
                bool save = r->hasParam("savepin", true)
                            && r->getParam("savepin", true)->value() == "true";
                if (save && strcmp(config.sim_pin, pin.c_str()) != 0) {
                    strlcpy(config.sim_pin, pin.c_str(), sizeof(config.sim_pin));
                    if (saveConfig())
                        doc["message"] = "PIN OK & saved.";
                    else
                        doc["message"] = "PIN OK, save fail.";
                }
                String s2; serializeJson(doc, s2);
                r->send(200, "application/json", s2);

                delay(3000);
                updateStatus();
                StaticJsonDocument<384> statusUpdateDoc;
                statusUpdateDoc["wifi_status"]  = (WiFi.status()==WL_CONNECTED) ? "Connected" : "Disconnected";
                statusUpdateDoc["ip_address"]   = WiFi.localIP().toString();
                statusUpdateDoc["sim_status"]   = simStatus;
                statusUpdateDoc["signal_quality"] = signalQuality;
                statusUpdateDoc["network_operator"] = networkOperator;
                statusUpdateDoc["sim_phone_number"] = simPhoneNumber;
                statusUpdateDoc["sim_pin_status"] = simRequiresPin ? (simPinOk ? "OK" : "Required") : "Not Required";
                String statusJsonString;
                serializeJson(statusUpdateDoc, statusJsonString);
                notifyClients("status", statusJsonString);
            }
            else {
                Serial.println("PIN Fail!");
                simPinOk = false;
                simRequiresPin = true;
                doc["success"] = false;
                doc["message"] = "PIN Reject/Err. Resp: " + resp;
                String s3; serializeJson(doc, s3);
                r->send(400, "application/json", s3);
            }
        }
        else {
            r->send(400, "application/json", R"({"success":false,"message":"Invalid PIN format"})");
        }
    }
    else {
        r->send(400, "application/json", R"({"success":false,"message":"'pin' parameter missing"})");
    }
});  

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *r)
              {Serial.println("Reboot req..."); r->send(200,"application/json",R"({"success":true,"message":"Reboot..."})"); delay(100); yield(); Serial.println("Restart now."); ESP.restart(); });
    // API Placeholders...
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r)
              {if(apMode){r->send(403);}else if(!simPinOk){r->send(503);}else{StaticJsonDocument<384> d; /*fill*/ r->send(200,"application/json","{}");} });
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *r)
              {if(apMode){r->send(403);}else{StaticJsonDocument<512> d; /*fill safe*/ r->send(200,"application/json","{}");} });
    server.on("/api/sms", HTTP_POST, [](AsyncWebServerRequest *r)
              {if(apMode){r->send(403);}else if(!simPinOk){r->send(503);}else if(r->hasParam("number",true)&&r->hasParam("message",true)){String n=r->getParam("number",true)->value(); String m=r->getParam("message",true)->value(); sendSMS(n,m); r->send(202,"application/json",R"({"status":"accepted"})");}else{r->send(400,"application/json",R"({"status":"error","message":"missing params"})");} });
    server.on("/api/ussd", HTTP_POST, [](AsyncWebServerRequest *r)
              {if(apMode){r->send(403);}else if(!simPinOk){r->send(503);}else if(r->hasParam("code",true)){String c=r->getParam("code",true)->value(); sendUSSD(c); r->send(202,"application/json",R"({"status":"accepted"})");}else{r->send(400,"application/json",R"({"status":"error","message":"missing code"})");} });
    server.onNotFound([](AsyncWebServerRequest *r)
                      {if(apMode){Serial.print("404(AP):H:");Serial.print(r->host());Serial.print(",U:");Serial.println(r->url()); bool isGW=(r->host()==currentIP); bool isAsset=r->url().endsWith(".css")||r->url().endsWith(".js")||r->url().endsWith(".ico"); if(!isGW&&!isAsset){AsyncWebServerResponse*p=r->beginResponse(302);p->addHeader("Location","http://"+currentIP);r->send(p);}else{r->send(404);}}else{r->send(404);} });
}

// --- WebSocket Handling ---

void notifyClients(const String &type, const String &data)
{
    bool isJson = (data.startsWith("{") && data.endsWith("}")) || (data.startsWith("[") && data.endsWith("]"));
    // *** INCREASED MAIN DOCUMENT SIZE for potentially large USSD hex data ***
    StaticJsonDocument<1536> doc; // Increased from 1024
    doc["type"] = type;
    if (isJson)
    {
        // Increase nested slightly too, just in case
        StaticJsonDocument<1024> nestedDoc;
        DeserializationError error = deserializeJson(nestedDoc, data);
        if (!error)
        {
            doc["data"] = nestedDoc;
        }
        else
        {
            Serial.printf("WARN:BadJSON '%s':%s\n", type.c_str(), error.c_str());
            doc["data"] = data; // Fallback to raw string
        }
    }
    else
    {
        doc["data"] = data;
    }
    String jsonString;
    size_t len = serializeJson(doc, jsonString);
    if (len > 0)
    {
        webSocket.broadcastTXT(jsonString);
    }
    else
    {
        Serial.println("WS Send Err: Serialization failed (Msg too large?).");
        // Optionally send a simplified error message if serialization fails
        webSocket.broadcastTXT("{\"type\":\"error\",\"data\":\"Message too large to send\"}");
    }
}

void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.printf("[%u]WS Disconn!\n", num);
        break;
    case WStype_CONNECTED:
    {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u]WS Conn:%s\n", num, ip.toString().c_str());
        Serial.println("Send init status...");
        StaticJsonDocument<384> sD;
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
        Serial.println("Init status sent.");
        Serial.println("Send init cfg...");
        StaticJsonDocument<512> cD;
        cD["server_host"] = config.server_host;
        cD["server_port"] = config.server_port;
        cD["server_user"] = config.server_user;
        cD["ap_password_set"] = (strlen(config.ap_password) >= 8);
        cD["sim_pin_set"] = (strlen(config.sim_pin) > 0);
        String cS;
        serializeJson(cD, cS);
        notifyClients("config", cS);
        Serial.println("Init cfg sent.");
    }
    break;
    case WStype_TEXT:
    {
        StaticJsonDocument<384> doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err)
        {
            Serial.print("WS JSON Err:");
            Serial.println(err.c_str());
            webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"Invalid JSON\"}");
            return;
        }
        const char *act = doc["action"];
        if (!act)
        {
            webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"'action' missing\"}");
            return;
        }
        Serial.printf("[%u]WS Action:%s\n", num, act);
        bool simN = (strcmp(act, "sendSMS") == 0 || strcmp(act, "sendUSSD") == 0 || strcmp(act, "sendUSSDReply") == 0);
        if (simN && !simPinOk)
        {
            Serial.printf("Act '%s' rej: SIM nrdy\n", act);
            webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"SIM nrdy\"}");
            return;
        }
        if (strcmp(act, "sendSMS") == 0)
        {
            Serial.println("DBG:Proc 'sendSMS'...");
            JsonVariantConst n_v = doc["number"];
            JsonVariantConst m_v = doc["message"];
            if (n_v.is<const char *>() && m_v.is<const char *>())
            {
                String nS = n_v.as<String>();
                String mS = m_v.as<String>();
                Serial.printf("DBG:Num='%s',Msg='%s'\n", nS.c_str(), mS.c_str());
                Serial.println("DBG:>>> Call sendSMS <<<");
                sendSMS(nS, mS);
                Serial.println("DBG:<<< Ret sendSMS <<<");
            }
            else
            {
                Serial.println("DBG:Inv SMS prms.");
                webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"Inv SMS prms\"}");
            }
            Serial.println("DBG:Fin 'sendSMS'.");
        }
        else if (strcmp(act, "sendUSSD") == 0)
        {
            Serial.println("DBG:Proc 'sendUSSD'...");
            JsonVariantConst c_v = doc["code"];
            if (c_v.is<const char *>())
            {
                String cS = c_v.as<String>();
                Serial.printf("DBG:Code='%s'\n", cS.c_str());
                Serial.println("DBG:>>> Call sendUSSD <<<");
                sendUSSD(cS);
                Serial.println("DBG:<<< Ret sendUSSD <<<");
            }
            else
            {
                Serial.println("DBG:Inv USSD prms.");
                webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"Inv USSD code\"}");
            }
            Serial.println("DBG:Fin 'sendUSSD'.");
        }
        else if (strcmp(act, "sendUSSDReply") == 0)
        {
            Serial.println("DBG:Proc 'sendUSSDReply'...");
            JsonVariantConst r_v = doc["reply"];
            if (r_v.is<const char *>())
            {
                String rS = r_v.as<String>();
                Serial.printf("DBG:Reply='%s'\n", rS.c_str());
                Serial.println("DBG:>>> Call sendUSSDReply <<<");
                sendUSSDReply(rS);
                Serial.println("DBG:<<< Ret sendUSSDReply <<<");
            }
            else
            {
                Serial.println("DBG:Inv USSD Reply prms.");
                webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"Inv USSD reply\"}");
            }
            Serial.println("DBG:Fin 'sendUSSDReply'.");
        }
        else if (strcmp(act, "getStatus") == 0)
        {
            Serial.println("DBG:Proc 'getStatus'...");
            StaticJsonDocument<384> s;
            s["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
            s["ip_address"] = WiFi.localIP().toString();
            s["sim_status"] = simStatus;
            s["signal_quality"] = signalQuality;
            s["network_operator"] = networkOperator;
            s["sim_phone_number"] = simPhoneNumber;
            s["sim_pin_status"] = simRequiresPin ? (simPinOk ? "OK" : "Required") : "Not Required";
            String ss;
            serializeJson(s, ss);
            notifyClients("status", ss);
            Serial.println("DBG:Fin 'getStatus'.");
        }
        else if (strcmp(act, "getConfig") == 0)
        {
            Serial.println("DBG:Proc 'getConfig'...");
            StaticJsonDocument<512> c;
            c["server_host"] = config.server_host;
            c["server_port"] = config.server_port;
            c["server_user"] = config.server_user;
            c["ap_password_set"] = (strlen(config.ap_password) >= 8);
            c["sim_pin_set"] = (strlen(config.sim_pin) > 0);
            String cs;
            serializeJson(c, cs);
            notifyClients("config", cs);
            Serial.println("DBG:Fin 'getConfig'.");
        }
        else if (strcmp(act, "getSMSList") == 0 || strcmp(act, "readSMS") == 0 || strcmp(act, "deleteSMS") == 0 || strcmp(act, "setCallForwarding") == 0 || strcmp(act, "getCallForwardingStatus") == 0)
        {
            Serial.printf("DBG:Act '%s' !impl.\n", act);
            webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"Act !impl\"}");
        }
        else
        {
            Serial.printf("Unk WS Act:%s\n", act);
            webSocket.sendTXT(num, "{\"type\":\"error\",\"data\":\"Unk action\"}");
        }
    }
    break;
    default:
        break;
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
                if (!silent)
                {
                    Serial.print("RX Line Check: ");
                    Serial.println(line);
                }
                if (expectedResponsePrefix && line.startsWith(expectedResponsePrefix))
                {
                    if (!silent)
                        Serial.println(" -> Found Prefix Line.");
                    relevantLine = line;
                    if (strcmp(expectedResponsePrefix, "OK") == 0 || strcmp(expectedResponsePrefix, "ERROR") == 0)
                        commandFinished = true;
                }
                else if (line.startsWith("OK"))
                {
                    if (!silent)
                        Serial.println(" -> Found OK Line.");
                    if (relevantLine.isEmpty())
                        relevantLine = line;
                    commandFinished = true;
                }
                else if (line.startsWith("ERROR") || line.indexOf("ERROR:") != -1)
                {
                    if (!silent)
                        Serial.println(" -> Found ERROR Line.");
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
        if (!silent)
        {
            Serial.printf("AT TIMEOUT '%s'(%lu ms)\n", cmd.c_str(), timeout);
            Serial.println("Full Buffer:" + responseBuffer);
        }
        relevantLine = "TIMEOUT";
    }
    if (!silent && responseBuffer.length() > 0)
    {
        responseBuffer.trim();
        if (responseBuffer.length() > 0)
            Serial.println("Remain buffer:" + responseBuffer);
    }
    return relevantLine;
}

// --- SIM Actions ---
void sendSMS(const String &number, const String &message)
{
    Serial.println(">>> DEBUG: Entered sendSMS function <<<");
    if (!simPinOk)
    {
        Serial.println("SMS fail: SIM nrdy");
        notifyClients("sms_sent", R"({"status":"ERROR","message":"SIM nrdy"})");
        return;
    }
    Serial.printf("Sending SMS to %s...\n", number.c_str());
    notifyClients("sms_status", R"({"status":"Sending..."})");
    Serial.println("DEBUG: Setting CMGF=1...");
    String cmgfResp = sendATCommand("AT+CMGF=1", 1500, "OK", false);
    if (!cmgfResp.startsWith("OK"))
    {
        Serial.println("ERR: Fail CMGF=1");
        notifyClients("sms_sent", R"({"status":"ERROR","message":"Fail mode"})");
        return;
    }
    delay(100);
    Serial.print("DEBUG: Sending CMGS cmd: ");
    Serial.println(number);
    while (sim900.available())
        sim900.read();
    simResponseBuffer = "";
    sim900.print("AT+CMGS=\"");
    sim900.print(number);
    sim900.println("\"");
    Serial.println("DEBUG: Wait '>' (buffered)...");
    String pBuf = "";
    unsigned long startP = millis();
    bool gotP = false;
    bool gotE = false;
    char rC;
    while (millis() - startP < 6000)
    {
        while (sim900.available())
        {
            rC = sim900.read();
            if (isPrintable(rC) || rC == '\r' || rC == '\n')
                pBuf += rC;
        }
        if (pBuf.indexOf("> ") != -1)
        {
            Serial.println("\nDEBUG: Got '>'!(Buf)");
            gotP = true;
            break;
        }
        if (pBuf.indexOf("ERROR") != -1)
        {
            Serial.println("\nDEBUG: ERR wait prompt!(Buf)");
            gotE = true;
            break;
        }
        yield();
        delay(20);
    }
    Serial.println("DEBUG: Prompt Buf:[" + pBuf + "]");
    StaticJsonDocument<128> resDoc;
    if (gotP)
    {
        delay(200);
        Serial.print("DEBUG: Send body+CtrlZ...");
        sim900.print(message.c_str());
        delay(200);
        sim900.write(26);
        sim900.println();
        Serial.println(" Sent.");
        Serial.println("DEBUG: Wait final OK/ERR(basic)...");
        String fLine = "";
        startP = millis();
        bool fin = false;
        while (millis() - startP < 30000)
        {
            if (sim900.available())
            {
                rC = sim900.read();
                if (rC == '\n')
                {
                    fLine.trim();
                    if (fLine.length() > 0)
                    {
                        Serial.println("DBG Final RX:" + fLine);
                        if (fLine.startsWith("OK") || fLine.indexOf("+CMGS:") != -1)
                        {
                            Serial.println("SMS OK.");
                            resDoc["status"] = "OK";
                            resDoc["message"] = "SMS sent.";
                            fin = true;
                            break;
                        }
                        else if (fLine.startsWith("ERROR") || fLine.indexOf("+CMS") != -1 || fLine.indexOf("+CME") != -1)
                        {
                            Serial.println("SMS Fail(ERR)");
                            resDoc["status"] = "ERROR";
                            resDoc["message"] = fLine;
                            fin = true;
                            break;
                        }
                    }
                    fLine = "";
                }
                else if (rC != '\r')
                    fLine += rC;
            }
            yield();
        }
        if (!fin)
        {
            Serial.println("SMS Fail(Timeout)");
            resDoc["status"] = "ERROR";
            resDoc["message"] = "Timeout send";
        }
    }
    else
    {
        Serial.println("DEBUG: No SMS prompt");
        resDoc["status"] = "ERROR";
        resDoc["message"] = gotE ? "ERR from modem" : "No SMS prompt";
    }
    String resS;
    serializeJson(resDoc, resS);
    notifyClients("sms_sent", resS);
    Serial.println(">>> DEBUG: Exit sendSMS <<<");
}

// --- Status Update (Simplified) ---

// --- SIM Actions (Reverting USSD to GSM mode) ---

void sendUSSD(const String &code)
{
    Serial.println(">>> DEBUG: Entered sendUSSD function (GSM Mode) <<<");
    if (!simPinOk)
    {
        Serial.println("USSD fail: SIM nrdy");
        notifyClients("ussd_response", "{\"type\":-1,\"message\":\"SIM nrdy\"}");
        return;
    }

    Serial.printf("Attempting to send USSD: %s\n", code.c_str());
    notifyClients("ussd_response", "{\"type\":-1,\"message\":\"Sending USSD...\"}");

    // --- Step 1: Ensure Character Set is GSM ---
    Serial.println("DEBUG: Setting CSCS=GSM...");
    String cscsResponse = sendATCommand("AT+CSCS=\"GSM\"", 1500, "OK", false);
    if (!cscsResponse.startsWith("OK"))
    {
        Serial.println("WARN: Failed to set CSCS=GSM, proceeding anyway...");
    }
    delay(100);

    // --- Step 2: Send USSD command with explicit DCS 15 ---
    String cmd = "AT+CUSD=1,\"" + code + "\",15"; // Add DCS 15 back
    while (sim900.available())
        sim900.read();
    simResponseBuffer = "";

    Serial.print("DEBUG: Sending USSD command (GSM mode)...");
    sim900.println(cmd); // Send the command
    Serial.println(" Sent.");

    delay(200);
    Serial.println(">>> DEBUG: Exiting sendUSSD function (GSM Mode) <<<");
    // Response (+CUSD:) handled by handleSimData()
}

void sendUSSDReply(const String &reply)
{
    Serial.println(">>> DEBUG: Entered sendUSSDReply function (GSM Mode) <<<");
    if (!simPinOk)
    {
        Serial.println("USSD Reply fail: SIM nrdy");
        notifyClients("ussd_response", "{\"type\":-1,\"message\":\"SIM !rdy reply\"}");
        return;
    }

    Serial.printf("Attempting to send USSD Reply: %s\n", reply.c_str());

    // --- Step 1: Ensure Character Set is GSM ---
    Serial.println("DEBUG: Setting CSCS=GSM (for reply)...");
    String cscsResponse = sendATCommand("AT+CSCS=\"GSM\"", 1500, "OK", false);
    if (!cscsResponse.startsWith("OK"))
    {
        Serial.println("WARN: Failed to set CSCS=GSM for reply!");
    }
    delay(100);

    // --- Step 2: Send USSD reply command with explicit DCS 15 ---
    String cmd = "AT+CUSD=1,\"" + reply + "\",15"; // Add DCS 15 back
    while (sim900.available())
        sim900.read();
    simResponseBuffer = "";

    Serial.print("DEBUG: Sending USSD Reply (GSM mode)...");
    sim900.println(cmd);
    Serial.println(" Sent.");

    delay(200);
    Serial.println(">>> DEBUG: Exiting sendUSSDReply function (GSM Mode) <<<");
    // Response (+CUSD:) handled by handleSimData()
}

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
    bool changed = false;
    bool copsOk = false;
    String copsLine = sendATCommand("AT+COPS?", 8000, "+COPS:", true);
    String newOp = "N/A";
    if (copsLine.startsWith("+COPS:"))
    {
        copsOk = true;
        int q1 = copsLine.indexOf('"');
        int q2 = copsLine.indexOf('"', q1 + 1);
        if (q1 != -1 && q2 != -1)
            newOp = copsLine.substring(q1 + 1, q2);
        else
        {
            int c1 = copsLine.indexOf(',');
            int c2 = copsLine.indexOf(',', c1 + 1);
            if (c1 != -1 && c2 != -1 && copsLine.substring(c1 + 1, c2).toInt() == 2)
            {
                int iq1 = copsLine.indexOf('"', c2);
                int iq2 = copsLine.indexOf('"', iq1 + 1);
                if (iq1 != -1 && iq2 != -1)
                    newOp = "ID:" + copsLine.substring(iq1 + 1, iq2);
                else
                    newOp = "Unk Fmt(Num)";
            }
            else
                newOp = "Unk Fmt(Op)";
        }
    }
    else
    {
        copsOk = false;
        if (copsLine != "TIMEOUT")
            Serial.println("COPS Err:" + copsLine);
        else
            Serial.println("COPS Timeout");
        newOp = "N/A(COPS Err)";
    }
    if (networkOperator != newOp)
    {
        networkOperator = newOp;
        changed = true;
    }
    delay(200);
    String csqLine = sendATCommand("AT+CSQ", 3000, "+CSQ:", true);
    String newSig = "N/A";
    bool csqOk = false;
    if (csqLine.startsWith("+CSQ:"))
    {
        csqOk = true;
        int cPos = csqLine.indexOf(',');
        if (cPos != -1)
        {
            int rssi = csqLine.substring(csqLine.indexOf(':') + 2, cPos).toInt();
            if (rssi >= 0 && rssi <= 31)
                newSig = String(-113 + (2 * rssi)) + " dBm";
            else if (rssi == 99)
                newSig = "Not detect";
            else
                newSig = "Inv code(" + String(rssi) + ")";
        }
        else
            newSig = "Parse Err(CSQ)";
    }
    else
    {
        if (csqLine != "TIMEOUT")
            Serial.println("CSQ Err:" + csqLine);
        else
            Serial.println("CSQ Timeout");
        newSig = "N/A(CSQ Err)";
    }
    if (signalQuality != newSig)
    {
        signalQuality = newSig;
        changed = true;
    }
    String newSt = "Unknown";
    if (copsOk)
        newSt = "Registered";
    else
        newSt = "Not Reg/Search...";
    if (simStatus != newSt)
    {
        simStatus = newSt;
        changed = true;
    }
    bool overallRdy = (simPinOk && copsOk);
    if (simPinOk != overallRdy)
    {
        Serial.printf("SIM readiness -> %s\n", overallRdy ? "YES" : "NO");
        simPinOk = overallRdy;
        changed = true;
    } /* CNUM commented out */
    if (changed)
    {
        Serial.printf("Status Update: SIM Ready=%s|St=%s|Sig=%s|Op=%s\n", simPinOk ? "Y" : "N", simStatus.c_str(), signalQuality.c_str(), networkOperator.c_str());
    }
}