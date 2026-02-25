#include "WiegandManager.h"
#include "search.h" 

extern CardDatabase db; 

WiegandManager::WiegandManager() : _hw(nullptr) {}

void wiegandTask(void* pvParameters) {
    WiegandManager* instance = (WiegandManager*)pvParameters;
    while(true) {
        instance->internalUpdate();
        delayMicroseconds(10); 
    }
}

void WiegandManager::init(JsonArray devices, HardwareManager* hw) {
    _hw = hw;
    for (JsonObject dev : devices) {
        if (dev["type"] == "wiegand") {
            WiegandReader* r = new WiegandReader();
            r->addr = dev["address"] | 34;
            r->pinD0 = dev["pins"][0];
            r->pinD1 = dev["pins"][1];
            r->group = dev["group"];
            _readers.push_back(r);
        }
    }
    
    xTaskCreatePinnedToCore(
        wiegandTask, "WiegandTask", 16384, this, 5, NULL, 1               
    );
    Serial.println("🚀 Wiegand Task started on Core 1");
}

void WiegandManager::internalUpdate() {
    if (!_hw) return;
    unsigned long now = millis();
    
    for (auto r : _readers) {
        uint8_t currentData = _hw->fastRead8(r->addr);
        
        bool d0 = (currentData >> r->pinD0) & 0x01;
        bool d1 = (currentData >> r->pinD1) & 0x01;

        if (d0 == LOW && r->lastD0 == HIGH) {
            r->cardCode <<= 1;
            r->bitCount++;
            r->lastBitTime = now;
        } 
        else if (d1 == LOW && r->lastD1 == HIGH) {
            r->cardCode = (r->cardCode << 1) | 1;
            r->bitCount++;
            r->lastBitTime = now;
        }

        r->lastD0 = d0;
        r->lastD1 = d1;

        if (r->bitCount > 0 && (now - r->lastBitTime > WIEGAND_TIMEOUT)) {
            handleCard(r);
        }
    }
}

void WiegandManager::handleCard(WiegandReader* r) {
    uint64_t cleanUID = (r->bitCount == 58) ? (r->cardCode >> 1) & 0xFFFFFFFFFFFFFFULL : r->cardCode;
    
    extern void onCardRead(uint64_t uid, int groupId);
    onCardRead(cleanUID, r->group);

    r->cardCode = 0;
    r->bitCount = 0;
}