#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

class ConfigManager {
public:
    ConfigManager();
    bool begin();
    JsonDocument& getDocument();

private:
    JsonDocument doc;
    const char* filename = "/config.json";
};

#endif