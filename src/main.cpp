#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "HardwareManager.h"
#include "WiegandManager.h"
#include "web.h"
#include "search.h"

JsonDocument config;
HardwareManager hw;
WiegandManager wiegand; 
CardDatabase db;      
WebHandler web(config, hw);
void printMemoryStats() {
    Serial.println("\n--- [ SYSTEM MEMORY INFO ] ---");

    size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    Serial.printf("RAM (Внутренняя): %u КБ свободно\n", freeInternal / 1024);
    Serial.printf("PSRAM (Внешняя):   %u КБ свободно\n", freePSRAM / 1024);

    size_t flashSize = ESP.getFlashChipSize();
    size_t sketchSize = ESP.getSketchSize();
    size_t freeSketchSpace = ESP.getFreeSketchSpace();

    Serial.printf("Flash (Всего):     %u МБ\n", flashSize / (1024 * 1024));
    Serial.printf("Прошивка занимает:  %u КБ\n", sketchSize / 1024);
    Serial.printf("Свободно под прошивку: %u КБ\n", freeSketchSpace / 1024);

    Serial.printf("LittleFS (Всего):  %u КБ\n", LittleFS.totalBytes() / 1024);
    Serial.printf("LittleFS (Занято): %u КБ\n", LittleFS.usedBytes() / 1024);

    Serial.println("------------------------------\n");
}
void onCardRead(uint64_t uid, int groupId) {
    Serial.printf("\n[Wiegand] Считыватель группы %d, UID: %llu\n", groupId, uid);
    
    CardResult result = db.find(uid);

    if (result.found) {
        if (result.status == 1) { 
            // Вывод времени в ms
            Serial.printf("✅ Доступ разрешен (Время поиска: %d ms)\n", result.search_time_us);

            int relayPin = -1;
            JsonArray relays = config["relays"].as<JsonArray>();
            for (JsonObject relay : relays) {
                if (relay["group"] == groupId) {
                    relayPin = relay["pin"] | -1;
                    break; 
                }
            }

            if (relayPin != -1) {
                Serial.printf("🔓 Открываем замок на пине %d (Группа %d)\n", relayPin, groupId);
                hw.pulsePCF(relayPin, LOW, 3000);
            } else {
                Serial.printf("⚠️ Реле для группы %d не найдено в конфиге!\n", groupId);
            }
        } else {
            Serial.printf("❌ Карта заблокирована! Статус: %d (Поиск: %d ms)\n", result.status, result.search_time_us);
        }
    } else {
        Serial.printf("❌ Карта %llu НЕ НАЙДЕНА (Поиск занял: %d ms)\n", uid, result.search_time_us);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- [ KINCONY A16 BOOT ] ---");

    // Инициализация файловой системы
    if (!LittleFS.begin()) {
        Serial.println("❌ Ошибка LittleFS! База данных недоступна.");
    }

    // Загрузка настроек
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
        deserializeJson(config, configFile);
        configFile.close();
        Serial.println("✅ Конфигурация загружена");
    }

    // Запуск базы данных и вывод первых 20 карт
    if (db.begin()) { 
        Serial.println("✅ База данных готова к работе");
    }

    hw.init(config);
    
    if (config["devices"].is<JsonArray>()) {
        Serial.printf("🔍 Найдено устройств: %d. Запуск Wiegand...\n", config["devices"].size());
        wiegand.init(config["devices"].as<JsonArray>(), &hw); 
    }

    web.begin();
    printMemoryStats();
    uint64_t lastCard = 0x11223344556677ULL; 
    Serial.println("--- ТЕСТ СКОРОСТИ (ПОСЛЕДНЯЯ КАРТА) ---");
    CardResult res = db.find(lastCard);

    if (res.found) {
        Serial.printf("✅ Найдена! UID: 0x%llX\n", res.uid);
        Serial.printf("⏱ Время поиска: %u us (микросекунд)\n", res.search_time_us);
        Serial.printf("📍 Источник: %s\n", res.source.c_str());
    } else {
        Serial.println("❌ Карта не найдена. Проверь формулу генерации в Python.");
    }   
    Serial.println("--- [ СИСТЕМА ГОТОВА ] ---");
    Serial.println("--- [ ГАЗ МИГАНИЯ ] ---");
    hw.pulsePCF(2, false, 10000);
}

void loop() {
    web.handle(); 
    hw.updateOutputs();
    delay(1);
}