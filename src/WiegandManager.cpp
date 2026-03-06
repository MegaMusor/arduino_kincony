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
        // Читаем состояние один раз за проход
        uint8_t currentData = _hw->fastRead8(r->addr);
        
        // В Wiegand импульс - это переход из HIGH в LOW
        bool d0 = (currentData >> r->pinD0) & 0x01;
        bool d1 = (currentData >> r->pinD1) & 0x01;

        // Обработка D0 (Бит 0)
        if (d0 == LOW && r->lastD0 == HIGH) {
            r->cardCode <<= 1;
            r->bitCount++;
            r->lastBitTime = now;
        } 
        // Обработка D1 (Бит 1) - НЕ используем else if, проверяем оба независимо
        if (d1 == LOW && r->lastD1 == HIGH) {
            r->cardCode = (r->cardCode << 1) | 1;
            r->bitCount++;
            r->lastBitTime = now;
        }

        r->lastD0 = d0;
        r->lastD1 = d1;

        // Увеличим таймаут до 100мс, чтобы точно дождаться конца медленных карт
        if (r->bitCount > 0 && (now - r->lastBitTime > 100)) {
            handleCard(r);
        }
    }
}

void WiegandManager::handleCard(WiegandReader* r) {
    uint64_t cleanUID = 0;

    if (r->bitCount == 26) {
        // Wiegand 26: убираем 1 бит четности в начале и 1 в конце
        cleanUID = (r->cardCode >> 1) & 0xFFFFFFULL;
    } 
    else if (r->bitCount == 34) {
        // Wiegand 34: убираем 1 бит четности в начале и 1 в конце
        cleanUID = (r->cardCode >> 1) & 0xFFFFFFFFULL;
    }
    else if (r->bitCount == 58) {
        // Твой текущий вариант для 58 бит
        cleanUID = (r->cardCode >> 1) & 0xFFFFFFFFFFFFFFULL;
    } 
    else {
        // Если длина нестандартная, берем как есть
        cleanUID = r->cardCode;
    }
    
    Serial.printf("[Wiegand] Raw Bits: %d, Clean UID: %llx\n", r->bitCount, cleanUID);

    extern void onCardRead(uint64_t uid, int groupId);
    onCardRead(cleanUID, r->group);

    r->cardCode = 0;
    r->bitCount = 0;
}