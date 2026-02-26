#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "HardwareManager.h"
#include "WiegandManager.h"
#include "web.h"
#include "search.h"
#include "dsl.h"

// Глобальные объекты
JsonDocument config;
HardwareManager hw;
WiegandManager wiegand; 
CardDatabase db;      
WebHandler web(config, hw);
DSLProcessor dsl(hw); 

void printMemoryStats() {
    Serial.println("\n--- [ MEMORY INFO ] ---");
    Serial.printf("RAM: %u KB | PSRAM: %u KB\n", 
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

void onCardRead(uint64_t uid, int groupId) {
    Serial.printf("\n[Wiegand] Card Read: %llu (Group %d)\n", uid, groupId);
    
    CardResult result = db.find(uid);

    if (result.found && result.status == 1) {
        bool actionExecuted = false;

        for (auto& ins : result.instructions) {
            // Строгое условие: выполняем только если action > 0
            if (ins.action > 0) {
                Serial.printf("🚀 DSL Action #%d triggered\n", ins.action);
                // Индексы в файле начинаются с 0, поэтому (action - 1)
                dsl.runActionFromFile(ins.action - 1); 
                actionExecuted = true;
            }
        }

        if (!actionExecuted) {
            Serial.println("⚠️ Доступ разрешен, но для этой карты/группы не назначен DSL Action (action=0)");
        }
    } else {
        Serial.printf("❌ Доступ запрещен или карта не найдена. UID: %llu\n", uid);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("--- KINCONY A16: DSL ENGINE (STRICT MODE) ---");

    if (!LittleFS.begin()) Serial.println("❌ LittleFS Error");

    // Загрузка конфига
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
        deserializeJson(config, configFile);
        configFile.close();
    }

    // Инициализация железа
    hw.init(config);
    
    // Загрузка БД карт в PSRAM
    if (db.begin()) Serial.println("✅ DB Loaded to PSRAM");

    // Инициализация DSL
    dsl.begin();

    // Запуск Wiegand
    if (config["devices"].is<JsonArray>()) {
        wiegand.init(config["devices"].as<JsonArray>(), &hw); 
    }

    web.begin();
    printMemoryStats();
}

void loop() {
    web.handle();          // Веб-сервер работает всегда
    hw.updateOutputs();    // Таймеры HardwareManager работают всегда
    dsl.tick();            // DSL работает параллельно!

    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            dsl.execute(input); // Просто кладет в очередь и сразу выходит
            Serial.println("📥 Command queued");
        }
    }
    
    yield(); // Даем время системным задачам ESP32
}