#ifndef WIEGAND_MANAGER_H
#define WIEGAND_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "HardwareManager.h"
#include <vector>
#include "map"

#define WIEGAND_TIMEOUT 40 

struct WiegandReader {
    uint8_t addr;
    uint8_t pinD0;
    uint8_t pinD1;
    int group;
    uint64_t cardCode = 0; 
    int bitCount = 0;
    unsigned long lastBitTime = 0;
    bool lastD0 = true;
    bool lastD1 = true;
};

class WiegandManager {
public:
    WiegandManager();
    void init(JsonArray devices, HardwareManager* hw);
    void internalUpdate(); 

private:
    std::vector<WiegandReader*> _readers;
    HardwareManager* _hw;
    void handleCard(WiegandReader* r);
};

#endif