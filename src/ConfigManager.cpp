#include "ConfigManager.h"

ConfigManager::ConfigManager() {}

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Mount Failed");
        return false;
    }

    if (!LittleFS.exists(filename)) {
        Serial.println("❌ Config file not found in LittleFS");
        return false;
    }

    File file = LittleFS.open(filename, "r");
    if (!file) return false;

    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.print("❌ JSON Error: ");
        Serial.println(error.c_str());
        return false;
    }
    return true;
}

JsonDocument& ConfigManager::getDocument() {
    return doc;
}