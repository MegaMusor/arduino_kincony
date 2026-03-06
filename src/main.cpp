#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "HardwareManager.h"
#include "WiegandManager.h"
#include "web.h"
#include "search.h"
#include "dsl.h"


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
    Serial.printf("\n[Wiegand] Card Read: %llx (Group %d)\n", uid, groupId);
    
    CardResult result = db.find(uid);

    if (result.found && result.status == 1) {
        bool actionExecuted = false;

        for (auto& ins : result.instructions) {
            if (ins.action > 0) {
                Serial.printf("🚀 DSL Action #%d triggered\n", ins.action);
                dsl.runActionFromFile(ins.action - 1); 
                actionExecuted = true;
            }
        }

        if (!actionExecuted) {
            Serial.println("⚠️ Доступ разрешен, но для этой карты/группы не назначен DSL Action (action=0)");
        }
    } else {
        Serial.printf("❌ Доступ запрещен или карта не найдена. UID: %llx\n", uid);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- KINCONY A16: DSL ENGINE (STRICT MODE) ---");

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

    Serial.println("\n--- [ ТЕСТ ПОИСКА КАРТЫ ] ---");
    uint64_t testUid = 0x0100000002468AULL; 
    // uint64_t testUid = 0x01000000ULL;
    CardResult res = db.find(testUid);

    if (res.found) {
        Serial.printf("✅ КАРТА НАЙДЕНА!\n");
        Serial.printf("ID: %llx\n", res.uid);
        Serial.printf("Статус: %d\n", res.status);
        Serial.printf("Группа: %d\n", res.group_id);
        Serial.printf("Лимит: %d\n", res.limit);
        Serial.printf("Источник: %s\n", res.source.c_str());
        Serial.printf("Время поиска: %u us\n", res.search_time_us);
        Serial.printf("Инструкций найдено: %d\n", res.instructions.size());

        for (size_t i = 0; i < res.instructions.size(); i++) {
            auto& ins = res.instructions[i];
            Serial.printf("  [%d] Action Index: %d, Priority: %d, Schedule: %d\n", 
                          i, ins.action, ins.priority, ins.schedule);
        }
    } else {
        Serial.printf("❌ Карта %llx НЕ НАЙДЕНА в базе данных.\n", testUid);
    }
    Serial.println("-----------------------------\n");
}

void loop() {
    web.handle();      
    hw.updateOutputs();   
    dsl.tick();           

    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            dsl.execute(input); 
            Serial.println("📥 Command queued");
        }
    }
    
    yield(); 
}