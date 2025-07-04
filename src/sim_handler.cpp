/**
 * @file sim_handler.cpp
 * @brief Implementation for all SIM900 module interactions.
 * @details This file contains all functions for initialization, sending commands,
 *          parsing responses, handling state machines for SMS, and managing the
 *          SIM900 module.
 */

#include "config.h"
#include "sim_handler.h"
#include "web_server.h" // Needed for notifyClients

// --- Forward declaration of functions used only within this file ---
static void handleSmsListLine(const String &line);
static void handleSmsSendLine(const String &line);
static String createPDU(const String &number, const String &message);


/**
 * @brief Initializes the SIM module with basic AT commands.
 */
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

/**
 * @brief Checks the SIM card's PIN status and attempts to unlock it if a PIN is saved.
 * @return true if the SIM is ready to use, false otherwise.
 */
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

/**
 * @brief Sends an AT command to the SIM module and waits for a specific response.
 * @param cmd The AT command to send.
 * @param timeout The maximum time to wait for a response in milliseconds.
 * @param expectedResponsePrefix The prefix of the line to be considered the final response.
 * @param silent If true, does not print the command to the Serial monitor.
 * @return The relevant response line from the module, or "TIMEOUT".
 */
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

/**
 * @brief Handles all incoming serial data from the SIM900 module.
 * @details This is a critical function that acts as a dispatcher. It parses each line,
 *          determines if it's an Unsolicited Result Code (URC) or a response to a command,
 *          and calls the appropriate handler.
 */
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

        // Special case for SMS prompt
        if (c == '>' && smsSendState == SMS_SEND_WAITING_PROMPT)
        {
            Serial.println("SIM RX: > (Prompt)");
            smsSendStartTime = millis();

            if (smsIsUnicode)
            {
                String pdu = createPDU(smsNumberToSend, smsMessageToSend);
                sim900.print(pdu);
                Serial.println("INFO: Sending PDU for Unicode text: " + pdu);
            }
            else
            {
                sim900.print(smsMessageToSend);
                Serial.println("INFO: Sending plain text: " + smsMessageToSend);
            }

            delay(100);
            sim900.write(26); // Ctrl+Z
            Serial.println("INFO: Message content sent. Awaiting final confirmation.");
            smsSendState = SMS_SEND_WAITING_FINAL_OK;
            return; // Exit immediately to avoid processing '>' as part of a line
        }

        // Process full lines ending with newline
        if (c == '\n')
        {
            simResponseBuffer.trim();
            if (simResponseBuffer.length() > 0)
            {
                // --- INTELLIGENT DISPATCHER LOGIC ---
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

/**
 * @brief Sends an SMS message. Switches between Text and PDU mode automatically for Unicode.
 * @param number The destination phone number.
 * @param message The message content.
 */
void sendSMS(const String &number, const String &message)
{
    if (smsListState != SMS_LIST_IDLE || smsSendState != SMS_SEND_IDLE)
    {
        notifyClients("error", "System is busy, please try again.");
        return;
    }

    if (message.length() == 0)
    {
        notifyClients("error", "Empty message");
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

    // Check for non-ASCII characters
    for (unsigned int i = 0; i < message.length(); i++)
    {
        unsigned char c = (unsigned char)message[i];
        if (c > 127)
        {
            smsIsUnicode = true;
            Serial.println("INFO: Unicode text detected - will use PDU mode.");
            break;
        }
    }

    // Clear pending serial data
    while (sim900.available() > 0)
    {
        sim900.read();
        yield();
    }

    smsSendState = SMS_SEND_SETTING_CHARSET;
    smsSendStartTime = millis();

    if (smsIsUnicode)
    {
        Serial.println("INFO: Setting PDU mode for Unicode text.");
        sim900.println("AT+CMGF=0"); // PDU mode
    }
    else
    {
        Serial.println("INFO: Setting Text mode for plain text.");
        sim900.println("AT+CMGF=1"); // Text mode
    }
}

/**
 * @brief Sends a USSD code.
 * @param code The USSD code to send (e.g., "*100#").
 */
void sendUSSD(const String &code)
{
    notifyClients("ussd_response", "{\"type\":-1,\"message\":\"Sending USSD...\"}");
    // Switch the modem character set to GSM, wait for the OK synchronously (handled by sendATCommand)
    sendATCommand("AT+CSCS=\"GSM\"", 1500, "OK", false);
    // IMPORTANT: never use delay() inside callbacks (e.g., HTTP/WS handlers) on ESPAsyncWebServer,
    // it can lead to watchdog resets and Exception(9) crashes. The call above already waits
    // synchronously for the modem response, so an extra delay isn’t necessary.
    delay(100);
    sim900.println("AT+CUSD=1,\"" + code + "\",15");
}

/**
 * @brief Sends a reply to an interactive USSD session.
 * @param reply The reply string.
 */
void sendUSSDReply(const String &reply)
{
    // Same rationale as sendUSSD(): avoid delay() inside potential web-server callbacks.
    sendATCommand("AT+CSCS=\"GSM\"", 1500, "OK", false);
    delay(100);
    sim900.println("AT+CUSD=1,\"" + reply + "\",15");
}

/**
 * @brief Reads a specific SMS message from storage.
 * @param index The storage index of the SMS to read.
 */
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
    body.trim();
    String decodedBody = decodeUcs2(body);
    doc["body"] = decodedBody;
    if (decodedBody != body)
        doc["body_hex"] = body;
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    notifyClients("sms_content", jsonOutput);
}

/**
 * @brief Deletes a specific SMS message from storage.
 * @param index The storage index of the SMS to delete.
 */
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

/**
 * @brief Fetches and updates the current network status.
 * @details Updates global variables for SIM status, signal quality, operator, etc.
 */
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

/**
 * @brief Decodes a UCS-2 hex string into a UTF-8 string.
 * @param hexStr The UCS-2 hex string (e.g., "063906310628064A").
 * @return The decoded UTF-8 string (e.g., "عربي").
 */
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

/**
 * @brief Encodes a UTF-8 string into a UCS-2 hex string.
 * @param utf8Str The UTF-8 string to encode.
 * @return The UCS-2 hex representation.
 */
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
                ucs2Char = 0x003F; // '?' for malformed UTF-8
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
                ucs2Char = 0x003F; // '?'
            }
        }
        else
        {
            ucs2Char = 0x003F; // '?'
        }

        char hex[5];
        sprintf(hex, "%04X", ucs2Char);
        hexStr += hex;
    }

    Serial.println("DEBUG: UTF-8 '" + utf8Str + "' -> UCS2 '" + hexStr + "'");
    return hexStr;
}

/**
 * @brief Starts the non-blocking process of retrieving the list of all SMS messages.
 */
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

/**
 * @brief (Static) Handles a line of response during the SMS listing process.
 * @param line The line received from the modem.
 */
static void handleSmsListLine(const String &line)
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
    else{

        //--- Important modification to avoid failure of the SMS list callback. ---
        // If the received line is not any of the above (not a message header, not a content,
        // not "OK" or "ERROR"), it is most likely an unexpected response (such as +CMT).
        // We print it and ignore it, allowing the operation to continue rather than failing.
        Serial.print("SMS List: Ignoring unexpected line: ");
        Serial.println(line);
    }
}

/**
 * @brief (Static) Handles a line of response during the SMS sending process.
 * @param line The line received from the modem.
 */
static void handleSmsSendLine(const String &line)
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
                String pdu = createPDU(smsNumberToSend, smsMessageToSend);
                int pduLengthWithoutSMSC = (pdu.length() - 2) / 2;
                sim900.print("AT+CMGS=");
                sim900.println(pduLengthWithoutSMSC);
                Serial.println("SIM TX: AT+CMGS=" + String(pduLengthWithoutSMSC));
            }
            else
            {
                sim900.print("AT+CMGS=\"");
                sim900.print(smsNumberToSend);
                sim900.println("\"");
                Serial.println("SIM TX: AT+CMGS=\"" + smsNumberToSend + "\"");
            }
        }
        else if (line.indexOf("ERROR") != -1)
        {
            Serial.println("ERROR: Failed to set SMS send mode");
            notifyClients("sms_sent", R"({"status":"ERROR","message":"Failed to set SMS mode","ar_message":"فشل في إعداد وضع الإرسال"})");
            smsSendState = SMS_SEND_IDLE;
        }
        break;

    case SMS_SEND_WAITING_PROMPT:
        if (line.indexOf("ERROR") != -1)
        {
             if (smsIsUnicode)
            {
                Serial.println("ERROR: Failed to start Arabic SMS send - PDU length or number error");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"Arabic PDU length error or invalid number","ar_message":"خطأ في طول PDU العربي أو رقم غير صالح"})");
            }
            else
            {
                Serial.println("ERROR: Failed to start English SMS send");
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
                Serial.println("INFO: Arabic SMS sent successfully!");
                notifyClients("sms_sent", R"({"status":"OK","message":"Arabic SMS sent successfully","ar_message":"تم إرسال الرسالة العربية بنجاح"})");
            }
            else
            {
                Serial.println("INFO: English SMS sent successfully!");
                notifyClients("sms_sent", R"({"status":"OK","message":"English SMS sent successfully","ar_message":"تم إرسال الرسالة الإنجليزية بنجاح"})");
            }
            smsSendState = SMS_SEND_IDLE;
        }
        else if (line.indexOf("ERROR") != -1)
        {
            if (smsIsUnicode)
            {
                Serial.println("ERROR: Arabic SMS failed to send - network or PDU error.");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"Arabic SMS network error or PDU format error","ar_message":"خطأ في الشبكة أو تنسيق PDU العربي"})");
            }
            else
            {
                Serial.println("ERROR: English SMS failed to send.");
                notifyClients("sms_sent", R"({"status":"ERROR","message":"English SMS failed","ar_message":"فشل في إرسال الرسالة الإنجليزية"})");
            }
            smsSendState = SMS_SEND_IDLE;
        }
        break;
    }
}

/**
 * @brief (Static) Creates a PDU (Protocol Data Unit) string for sending Unicode SMS.
 * @param number The destination phone number.
 * @param message The UTF-8 message to be encoded.
 * @return A string containing the full PDU packet.
 */
static String createPDU(const String &number, const String &message)
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