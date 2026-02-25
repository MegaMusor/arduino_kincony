#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include "LittleFS.h"

class HardwareManager {
public:
    HardwareManager();
    void init(JsonDocument& config);
    void initEthernet(JsonDocument& config);
    
    void digitalWritePCF(uint8_t pin, bool state);
    void pulsePCF(uint8_t pin, bool state, uint32_t durationMs);
    void updateOutputs(); 

    uint8_t fastRead8(uint8_t address);
    bool saveConfig(JsonDocument& config);

private:
    bool _initialized = false;
    bool _ethStarted = false;
    SemaphoreHandle_t _i2cMutex;

    // Состояние портов
    uint8_t _portA = 0xFF; // 0x24
    uint8_t _portB = 0xFF; // 0x25
    bool _needUpdateA = false;
    bool _needUpdateB = false;

    struct OutputTimer {
        uint8_t pin;
        uint32_t endTime;
        bool idleState;
        bool active = false;
    };
    OutputTimer _timers[16];
};

#endif