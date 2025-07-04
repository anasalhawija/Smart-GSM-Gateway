/**
 * @file    file_system.cpp
 * @author  Eng: Anas Alhawija
 * @brief   Implementation of filesystem and configuration management.
 * @version 2.1
 * @date    2025-07-04
 * 
 * @project Smart GSM Gateway
 * @license MIT License
 * 
 * @description Implements the logic for mounting the LittleFS filesystem, formatting it
 *              if necessary, and handling the serialization/deserialization of the
 *              configuration to and from a JSON file.
 */

 

/**
 * @file file_system.cpp
 * @brief Implementation of filesystem operations.
 */

#include "config.h"
#include "file_system.h"

/**
 * @brief Initializes the LittleFS filesystem.
 * @details Mounts the filesystem. If mounting fails, it formats the filesystem.
 *          Halts execution on a format failure.
 */
void initFileSystem() {
    Serial.println("Initializing LittleFS...");
    if (!LittleFS.begin()) {
        Serial.println("Filesystem mount failed! Attempting to format...");
        if (LittleFS.format()) {
            Serial.println("Filesystem formatted successfully.");
        } else {
            Serial.println("FATAL: Filesystem format failed!");
            while (1) delay(1000); // Halt
        }
    } else {
        Serial.println("LittleFS mounted successfully.");
    }
}

/**
 * @brief Loads the configuration from the JSON file in LittleFS.
 * @return true if configuration was loaded successfully, false otherwise.
 */
bool loadConfig() {
    if (LittleFS.exists(CONFIG_FILE)) {
        File f = LittleFS.open(CONFIG_FILE, "r");
        if (!f) {
            Serial.println("Failed to open config file for reading.");
            return false;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) {
            Serial.print("Failed to parse config file: ");
            Serial.println(err.c_str());
            return false;
        }
        // Copy data from JSON to config struct
        strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
        strlcpy(config.wifi_password, doc["wifi_password"] | "", sizeof(config.wifi_password));
        strlcpy(config.ap_password, doc["ap_password"] | "", sizeof(config.ap_password));
        strlcpy(config.server_host, doc["server_host"] | "", sizeof(config.server_host));
        config.server_port = doc["server_port"] | 0;
        strlcpy(config.server_user, doc["server_user"] | "", sizeof(config.server_user));
        strlcpy(config.server_pass, doc["server_pass"] | "", sizeof(config.server_pass));
        strlcpy(config.sim_pin, doc["sim_pin"] | "", sizeof(config.sim_pin));
        Serial.println("Configuration loaded from file.");
        return true;
    }
    Serial.println("No configuration file found.");
    return false;
}

/**
 * @brief Saves the current configuration to the JSON file in LittleFS.
 * @return true if the configuration was saved successfully, false otherwise.
 */
bool saveConfig() {
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
    if (!f) {
        Serial.println("Failed to open config file for writing.");
        return false;
    }
    size_t bytesWritten = serializeJson(doc, f);
    f.close();
    if (bytesWritten > 0) {
        Serial.println("Configuration saved successfully.");
        return true;
    }
    Serial.println("Failed to write configuration to file.");
    return false;
}