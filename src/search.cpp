#include "search.h"

CardDatabase::CardDatabase() {}

bool CardDatabase::begin() {
    Serial.println("🚚 Инициализация сверхбыстрой БД (PSRAM)...");
    
    // Порядок важен: сначала карты, потом группы, потом правила
    if (!loadCards()) { Serial.println("❌ Ошибка загрузки карт"); return false; }
    if (!loadGroups()) { Serial.println("❌ Ошибка загрузки групп"); return false; }
    if (!loadRules()) { Serial.println("❌ Ошибка загрузки правил"); return false; }
    
    Serial.printf("✅ БД загружена: %u карт, %u групп, %u правил\n", _total_cards, _total_groups, _total_rules);
    return true;
}

// 1. ЗАГРУЗКА КАРТ (Маркер 08/09 -> 9 байт в PSRAM)
bool CardDatabase::loadCards() {
    File f = LittleFS.open("/cards.bin", "r");
    if (!f) return false;

    // Резервируем память под 300 000 карт (7 байт ID + 2 байта флагов)
    _all_cards = (uint8_t*)heap_caps_malloc(300000 * 9, MALLOC_CAP_SPIRAM);
    if (!_all_cards) return false;

    uint32_t i = 0;
    while(f.available() && i < 300000) {
        uint8_t entrySize = f.read(); // Считываем маркер (08 или 09)
        uint8_t* dest = _all_cards + (i * 9);
        
        if (entrySize == 8) {
            // Читаем 7 байт ID + 1 байт флага
            f.read(dest, 8); 
            uint8_t f8 = dest[7];
            // Нормализация флага в 16 бит (для совместимости с 64+ картами)
            uint16_t f16 = ((uint16_t)(f8 >> 6) << 14) | (f8 & 0x3F);
            dest[7] = (f16 >> 8) & 0xFF;
            dest[8] = f16 & 0xFF;
        } else if (entrySize == 9) {
            // Читаем 7 байт ID + 2 байта флага напрямую
            f.read(dest, 9); 
        }
        i++;
    }
    _total_cards = i;
    f.close();
    return true;
}

// 2. ЗАГРУЗКА ГРУПП (Динамические блоки с bSize)
bool CardDatabase::loadGroups() {
    File f = LittleFS.open("/groups.bin", "r");
    if (!f) return false;

    // Считаем общий объем инструкций и количество групп
    size_t total_instr_count = 0;
    uint32_t g_count = 0;
    
    while(f.available()) {
        uint16_t bSize; 
        f.read((uint8_t*)&bSize, 2);
        bSize = (bSize << 8) | (bSize >> 8); // Swap endianness
        total_instr_count += (bSize / 2);
        f.seek(f.position() + bSize);
        g_count++;
    }
    _total_groups = g_count;

    // Выделяем память
    _all_groups = (uint16_t*)heap_caps_malloc(total_instr_count * 2, MALLOC_CAP_SPIRAM);
    _group_offsets = (uint32_t*)heap_caps_malloc(_total_groups * 4, MALLOC_CAP_SPIRAM);
    _group_lens = (uint8_t*)heap_caps_malloc(_total_groups, MALLOC_CAP_SPIRAM);

    if (!_all_groups || !_group_offsets) return false;

    f.seek(0);
    uint32_t current_instr_offset = 0;
    for(uint32_t g = 0; g < _total_groups; g++) {
        uint16_t bSize; 
        f.read((uint8_t*)&bSize, 2);
        bSize = (bSize << 8) | (bSize >> 8);
        
        _group_lens[g] = bSize / 2;
        _group_offsets[g] = current_instr_offset;
        
        // Читаем индексы инструкций
        f.read((uint8_t*)(_all_groups + current_instr_offset), bSize);
        
        // Исправляем endianness для каждого индекса
        for(int n = 0; n < _group_lens[g]; n++) {
            uint16_t val = _all_groups[current_instr_offset + n];
            _all_groups[current_instr_offset + n] = (val << 8) | (val >> 8);
        }
        current_instr_offset += _group_lens[g];
    }
    f.close();
    return true;
}

// 3. ЗАГРУЗКА ПРАВИЛ (4 байта на правило)
bool CardDatabase::loadRules() {
    File f = LittleFS.open("/rules.bin", "r");
    if (!f) return false;

    size_t sz = f.size();
    _total_rules = sz / 4;
    _rules_table = (uint32_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    
    if (!_rules_table) return false;

    for(uint32_t r = 0; r < _total_rules; r++) {
        uint32_t raw;
        f.read((uint8_t*)&raw, 4);
        // Swap 32-bit endianness
        _rules_table[r] = __builtin_bswap32(raw);
    }
    f.close();
    return true;
}

uint64_t CardDatabase::getCardID(uint32_t idx) {
    uint64_t id = 0;
    uint8_t* p = _all_cards + (idx * 9);
    for (int i = 0; i < 7; i++) id = (id << 8) | p[i];
    return id;
}

Instruction CardDatabase::unpackInstruction(uint32_t r) {
    Instruction ins;
    // Твоя битовая маска
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

    if (_total_cards == 0) return res;

    int32_t low = 0, high = _total_cards - 1;
    while (low <= high) {
        int32_t mid = low + (high - low) / 2;
        uint64_t midId = getCardID(mid);

        if (midId == uid) {
            res.found = true;
            uint8_t* p = _all_cards + (mid * 9) + 7;
            uint16_t flags = (p[0] << 8) | p[1];
            
            res.status = 1; 
            res.group_id = flags & 0x3FFF;
            res.limit = (flags >> 14) & 0x03;

            // Извлекаем инструкции для этой группы
            if (res.group_id < _total_groups) {
                uint32_t off = _group_offsets[res.group_id];
                for(int k = 0; k < _group_lens[res.group_id]; k++) {
                    uint16_t ruleIdx = _all_groups[off + k];
                    if (ruleIdx < _total_rules) {
                        res.instructions.push_back(unpackInstruction(_rules_table[ruleIdx]));
                    }
                }
            }
            break;
        }
        if (midId < uid) low = mid + 1;
        else high = mid - 1;
    }

    res.search_time_us = micros() - startTime;
    return res;
}