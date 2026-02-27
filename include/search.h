#ifndef SEARCH_H
#define SEARCH_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "esp_heap_caps.h"

struct Instruction {
    uint8_t mask;      
    uint8_t count;     
    uint16_t schedule; 
    uint8_t priority;  
    bool polarity;     
    uint8_t action;    
};

struct CardResult {
    bool found = false;
    uint64_t uid = 0;
    uint8_t status = 0;     
    uint8_t limit = 0;
    uint16_t group_id = 0;
    std::vector<Instruction> instructions;
    uint32_t search_time_us = 0;
    String source = "PSRAM";
};

class CardDatabase {
public:
    CardDatabase();
    bool begin();
    CardResult find(uint64_t uid);
    
private:
    // Массивы карт
    uint8_t* _cards34 = nullptr;    
    uint8_t* _cards56 = nullptr;
    
    // Таблицы групп и правил
    uint16_t* _all_groups = nullptr;   
    uint32_t* _group_offsets = nullptr; 
    uint8_t* _group_lens = nullptr;    
    uint32_t* _rules_table = nullptr;  

    uint32_t _total34 = 0;
    uint32_t _total56 = 0;
    uint32_t _total_groups = 0;
    uint32_t _total_rules = 0;

    bool loadCards();
    bool loadGroups();
    bool loadRules();
    
    uint64_t getID34(uint32_t idx);
    uint64_t getID56(uint32_t idx);
    Instruction unpackInstruction(uint32_t raw);
};

#endif