/**
 * @file    sim_handler.h
 * @author  Eng: Anas Alhawija
 * @brief   Function prototypes for all SIM900 module interactions.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description Declares the public interface for the SIM handler module, including functions
 *              for initialization, status checking, sending AT commands, and performing
 *              actions like sending SMS and USSD codes.
 */


/**
 * @file sim_handler.h
 * @brief Function prototypes for all SIM900 module interactions.
 */

#ifndef SIM_HANDLER_H
#define SIM_HANDLER_H

#include <Arduino.h>

// --- Initialization and Status ---
void initializeSIM();
bool checkSimPin();
void updateStatus();

// --- Core Communication ---
String sendATCommand(const String &cmd, unsigned long timeout, const char *expectedResponsePrefix, bool silent = false);
void handleSimData();

// --- SIM Actions ---
void sendSMS(const String &number, const String &message);
void sendUSSD(const String &code);
void sendUSSDReply(const String &reply);
void readSMS(int index);
void deleteSMS(int index);
void startGetSmsList();

// --- Helper Functions ---
String decodeUcs2(const String &hexStr);
String encodeUcs2(const String &utf8Str);

#endif // SIM_HANDLER_H