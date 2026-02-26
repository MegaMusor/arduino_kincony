#include "dsl.h"

DSLProcessor::DSLProcessor(HardwareManager& hw) : _hw(hw) {}

void DSLProcessor::begin() {
    Serial.println("🚀 DSL Async Engine Ready");
}

void DSLProcessor::execute(String line) {
    // Если пришла новая команда из консоли, возможно, стоит очистить старую очередь?
    // _queue.clear(); // Раскомментируй, если хочешь, чтобы новая команда прерывала старую

    int start = 0;
    while (start < line.length()) {
        int end = line.indexOf(';', start);
        String sub;
        if (end == -1) {
            sub = line.substring(start);
            start = line.length();
        } else {
            sub = line.substring(start, end);
            start = end + 1;
        }
        
        sub.trim();
        if (sub.length() > 0) {
            QueuedCommand cmd = parse(sub);
            if (cmd.action != ACTION_NONE) {
                _queue.push_back(cmd);
            }
        }
    }
}

void DSLProcessor::tick() {
    if (_queue.empty() || _isWaiting) {
        if (_isWaiting && millis() >= _nextStepTime) {
            _isWaiting = false;
            // Serial.println("⏳ Sleep finished");
        }
        return;
    }

    QueuedCommand cmd = _queue.front();
    _queue.pop_front();

    if (cmd.action == ACTION_SLEEP) {
        _isWaiting = true;
        _nextStepTime = millis() + cmd.duration;
        Serial.printf("⏳ Sleeping for %d ms...\n", cmd.duration);
    } else {
        Serial.printf("🎬 Executing Action: %d, Pins: %04X\n", cmd.action, cmd.pinMask);
        applyImmediate(cmd);
    }
}

QueuedCommand DSLProcessor::parse(String subLine) {
    QueuedCommand cmd;
    subLine.trim();
    
    // Разбиваем подкоманду на части по пробелам
    std::vector<String> parts;
    int p_start = 0;
    while(p_start < subLine.length()){
        int p_space = subLine.indexOf(' ', p_start);
        if(p_space == -1){
            parts.push_back(subLine.substring(p_start));
            break;
        }
        parts.push_back(subLine.substring(p_start, p_space));
        p_start = p_space + 1;
    }

    if (parts.empty()) return cmd;

    String actionStr = parts[0];
    actionStr.toUpperCase();

    if (actionStr == "OPEN") cmd.action = ACTION_OPEN;
    else if (actionStr == "CLOSE") cmd.action = ACTION_CLOSE;
    else if (actionStr == "SLEEP") {
        cmd.action = ACTION_SLEEP;
        if (parts.size() > 1) cmd.duration = parts[1].toInt();
        return cmd;
    } else return cmd;

    // Парсим пины для OPEN/CLOSE
    for (size_t i = 1; i < parts.size(); i++) {
        String p = parts[i];
        p.toUpperCase();
        if (p == "ALL") {
            cmd.pinMask = 0xFFFF;
            break;
        }
        int pinNum = p.toInt();
        if (pinNum >= 1 && pinNum <= 16) {
            cmd.pinMask |= (1 << (pinNum - 1));
        }
    }

    return cmd;
}

void DSLProcessor::applyImmediate(QueuedCommand cmd) {
    for (int i = 0; i < 16; i++) {
        if (cmd.pinMask & (1 << i)) {
            _hw.digitalWritePCF(i, (cmd.action == ACTION_CLOSE));
        }
    }
    _hw.updateOutputs(); 
}
void DSLProcessor::clearQueue() {
    _queue.clear();
    _isWaiting = false;
    _nextStepTime = 0;
}
void DSLProcessor::runActionFromFile(int actionIdx) {
    File f = LittleFS.open("/actions.bin", "r");
    if (!f) {
        Serial.println("❌ DSL Error: actions.bin not found");
        return;
    }

    int currentIdx = 0;
    bool found = false;

    while (f.available()) {
        uint8_t len = f.read(); // Читаем 1 байт длины
        
        if (currentIdx == actionIdx) {
            // Выделяем память под строку + нуль-терминатор
            char* buf = (char*)malloc(len + 1);
            if (buf) {
                f.readBytes(buf, len);
                buf[len] = '\0';
                
                String cmdLine = String(buf);
                Serial.printf("📂 Loaded from file (Index %d): %s\n", actionIdx, buf);
                
                // Отправляем в наш новый асинхронный execute
                this->execute(cmdLine);
                
                free(buf);
                found = true;
            }
            break; 
        } else {
            // Пропускаем байты этой команды, чтобы перейти к следующей
            f.seek(f.position() + len);
        }
        currentIdx++;
    }

    if (!found) {
        Serial.printf("⚠️ DSL Warning: Action index %d not found in file\n", actionIdx);
    }

    f.close();
}