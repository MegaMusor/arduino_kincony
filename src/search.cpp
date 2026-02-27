#include "search.h"

CardDatabase::CardDatabase() {}

bool CardDatabase::begin() {
    Serial.println("\n--- [ DATABASE STARTUP ] ---");
    
    if (!loadCards()) { Serial.println("❌ Error loading Cards"); return false; }
    if (!loadGroups()) { Serial.println("❌ Error loading Groups"); return false; }
    if (!loadRules()) { Serial.println("❌ Error loading Rules"); return false; }
    
    Serial.println("--- [ DATABASE READY ] ---\n");
    return true;
}

bool CardDatabase::loadCards() {
    // Загрузка 34-бит
    if (LittleFS.exists("/cards34.bin")) {
        File f = LittleFS.open("/cards34.bin", "r");
        size_t sz = f.size();
        _total34 = sz / 6;
        _cards34 = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (_cards34) f.read(_cards34, sz);
        f.close();
        Serial.printf("✅ Loaded 34-bit cards: %u\n", _total34);
    }

    // Загрузка 56-бит
    if (LittleFS.exists("/cards56.bin")) {
        File f = LittleFS.open("/cards56.bin", "r");
        size_t sz = f.size();
        _total56 = sz / 9;
        _cards56 = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (_cards56) f.read(_cards56, sz);
        f.close();
        Serial.printf("✅ Loaded 56-bit cards: %u\n", _total56);
    }
    return (_cards34 || _cards56);
}

bool CardDatabase::loadGroups() {
    if (!LittleFS.exists("/groups.bin")) return false;
    File f = LittleFS.open("/groups.bin", "r");
    
    uint32_t expected_groups = 16384;
    _total_groups = 0;
    size_t total_instr_in_file = 0;

    // 1. Первый проход: Считаем точное кол-во инструкций для аллокации
    while (f.available() >= 2) {
        uint16_t bSize;
        f.read((uint8_t*)&bSize, 2);
        // Переводим из Big Endian (из файла) в формат процессора
        bSize = (bSize << 8) | (bSize >> 8); 
        
        total_instr_in_file += (bSize / 2);
        f.seek(f.position() + bSize); // Прыгаем через блок данных
        _total_groups++;
    }

    Serial.printf("🔍 Считано из файла: %u групп, %u инструкций\n", _total_groups, total_instr_in_file);

    // 2. Выделяем память
    _all_groups = (uint16_t*)heap_caps_malloc(total_instr_in_file * 2, MALLOC_CAP_SPIRAM);
    _group_offsets = (uint32_t*)heap_caps_malloc(_total_groups * 4, MALLOC_CAP_SPIRAM);
    _group_lens = (uint8_t*)heap_caps_malloc(_total_groups, MALLOC_CAP_SPIRAM);

    if (!_all_groups || !_group_offsets || !_group_lens) {
        Serial.println("❌ Ошибка памяти PSRAM для групп");
        f.close();
        return false;
    }

    // 3. Второй проход: Загружаем данные
    f.seek(0);
    uint32_t current_instr_idx = 0;
    for (uint32_t i = 0; i < _total_groups; i++) {
        uint16_t bSize;
        f.read((uint8_t*)&bSize, 2);
        bSize = (bSize << 8) | (bSize >> 8);

        _group_lens[i] = bSize / 2;
        _group_offsets[i] = current_instr_idx;

        // Читаем все индексы этой группы за один раз
        f.read((uint8_t*)(_all_groups + current_instr_idx), bSize);

        // Исправляем endianness для каждого индекса в группе
        for (int n = 0; n < _group_lens[i]; n++) {
            uint16_t raw_idx = _all_groups[current_instr_idx + n];
            _all_groups[current_instr_idx + n] = (raw_idx << 8) | (raw_idx >> 8);
        }
        current_instr_idx += _group_lens[i];
    }

    f.close();
    Serial.printf("✅ Успешно загружено групп: %u\n", _total_groups);
    return true;
}

bool CardDatabase::loadRules() {
    if (!LittleFS.exists("/rules.bin")) return false;
    File f = LittleFS.open("/rules.bin", "r");
    size_t sz = f.size();
    _total_rules = sz / 4;
    _rules_table = (uint32_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!_rules_table) return false;
    
    for(uint32_t i=0; i<_total_rules; i++) {
        uint32_t raw;
        f.read((uint8_t*)&raw, 4);
        _rules_table[i] = __builtin_bswap32(raw);
    }
    f.close();
    Serial.printf("✅ Loaded rules: %u\n", _total_rules);
    return true;
}

uint64_t CardDatabase::getID34(uint32_t idx) {
    uint64_t id = 0;
    uint8_t* p = _cards34 + (idx * 6);
    for (int i = 0; i < 4; i++) id = (id << 8) | p[i];
    return id;
}

uint64_t CardDatabase::getID56(uint32_t idx) {
    uint64_t id = 0;
    uint8_t* p = _cards56 + (idx * 9);
    for (int i = 0; i < 7; i++) id = (id << 8) | p[i];
    return id;
}

Instruction CardDatabase::unpackInstruction(uint32_t r) {
    Instruction ins;
    ins.mask     = (r >> 24) & 0xFF;  
    ins.count    = (r >> 22) & 0x03;  
    ins.schedule = (r >> 13) & 0x1FF; 
    ins.priority = (r >> 5)  & 0xFF;  
    ins.polarity = (r >> 4)  & 0x01;  
    ins.action   = r & 0x0F;          
    return ins;
}

CardResult CardDatabase::find(uint64_t uid) {
    uint32_t startTime = micros();
    CardResult res;
    res.uid = uid;
    res.found = false;

    // 1. Поиск в 34-битном массиве
    if (uid <= 0xFFFFFFFFULL && _total34 > 0) {
        int32_t low = 0, high = _total34 - 1;
        while (low <= high) {
            int32_t mid = low + (high - low) / 2;
            uint64_t midId = getID34(mid);
            if (midId == uid) {
                res.found = true;
                uint8_t* p = _cards34 + (mid * 6) + 4;
                uint16_t flags = (p[0] << 8) | p[1];
                res.group_id = flags & 0x3FFF;
                res.limit = (flags >> 14) & 0x03;
                res.source = "PSRAM-34";
                break;
            }
            if (midId < uid) low = mid + 1; else high = mid - 1;
        }
    }

    // 2. Поиск в 56-битном массиве
    if (!res.found && _total56 > 0) {
        int32_t low = 0, high = _total56 - 1;
        while (low <= high) {
            int32_t mid = low + (high - low) / 2;
            uint64_t midId = getID56(mid);
            if (midId == uid) {
                res.found = true;
                uint8_t* p = _cards56 + (mid * 9) + 7;
                uint16_t flags = (p[0] << 8) | p[1];
                res.group_id = flags & 0x3FFF;
                res.limit = (flags >> 14) & 0x03;
                res.source = "PSRAM-56";
                break;
            }
            if (midId < uid) low = mid + 1; else high = mid - 1;
        }
    }

    // 3. Сбор инструкций
    if (res.found && res.group_id < _total_groups) {
        res.status = 1;
        uint32_t off = _group_offsets[res.group_id];
        for(int k = 0; k < _group_lens[res.group_id]; k++) {
            uint16_t ruleIdx = _all_groups[off + k];
            if (ruleIdx < _total_rules) {
                res.instructions.push_back(unpackInstruction(_rules_table[ruleIdx]));
            }
        }
    }

    res.search_time_us = micros() - startTime;
    return res;
}