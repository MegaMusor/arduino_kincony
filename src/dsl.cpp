#include "dsl.h"

DSLProcessor::DSLProcessor(HardwareManager& hw) : _hw(hw) {}

void DSLProcessor::begin() {
    Serial.println("🚀 DSL Multi-Tasking Engine Ready");
}

// Запуск новой параллельной задачи
void DSLProcessor::execute(String line) {
    DSLInstance newProcess;
    
    int start = 0;
    while (start < line.length()) {
        int end = line.indexOf(';', start);
        String sub = (end == -1) ? line.substring(start) : line.substring(start, end);
        sub.trim();
        
        if (sub.length() > 0) {
            QueuedCommand cmd = parseSubCommand(sub);
            if (cmd.action != ACTION_NONE) {
                newProcess.commands.push_back(cmd);
            }
        }
        
        if (end == -1) break;
        start = end + 1;
    }

    if (!newProcess.commands.empty()) {
        _activeInstances.push_back(newProcess);
        Serial.printf("➕ Started parallel task. Active tasks: %d\n", _activeInstances.size());
    }
}

// Главный цикл обработки всех запущенных сценариев
void DSLProcessor::tick() {
    if (_activeInstances.empty()) return;

    auto it = _activeInstances.begin();
    while (it != _activeInstances.end()) {
        // 1. Проверяем, не находится ли данный процесс в режиме сна
        if (it->isWaiting) {
            if (millis() >= it->nextStepTime) {
                it->isWaiting = false;
            } else {
                ++it; // Процесс еще спит, идем к следующему в списке
                continue;
            }
        }

        // 2. Выполняем следующую команду процесса
        if (!it->commands.empty()) {
            QueuedCommand cmd = it->commands.front();
            it->commands.pop_front();

            if (cmd.action == ACTION_SLEEP) {
                it->isWaiting = true;
                it->nextStepTime = millis() + cmd.duration;
            } else {
                // Применяем OPEN или CLOSE к пинам
                for (int i = 0; i < 16; i++) {
                    if (cmd.pinMask & (1 << i)) {
                        _hw.digitalWritePCF(i, (cmd.action == ACTION_CLOSE));
                    }
                }
                _hw.updateOutputs();
            }
        }

        // 3. Если команды закончились и мы не ждем — удаляем процесс
        if (it->commands.empty() && !it->isWaiting) {
            it = _activeInstances.erase(it);
            Serial.println("➖ Task finished.");
        } else {
            ++it;
        }
    }
}

// Парсинг строки в структуру команды
QueuedCommand DSLProcessor::parseSubCommand(String sub) {
    QueuedCommand q;
    sub.trim();
    
    // Делим на части по пробелам для удобства (например: "OPEN 1 2 3")
    std::vector<String> parts;
    int p_start = 0;
    while(p_start < sub.length()){
        int p_space = sub.indexOf(' ', p_start);
        if(p_space == -1){
            parts.push_back(sub.substring(p_start));
            break;
        }
        parts.push_back(sub.substring(p_start, p_space));
        p_start = p_space + 1;
    }

    if (parts.empty()) return q;

    String actionStr = parts[0];
    actionStr.toUpperCase();

    if (actionStr == "OPEN") q.action = ACTION_OPEN;
    else if (actionStr == "CLOSE") q.action = ACTION_CLOSE;
    else if (actionStr == "SLEEP") {
        q.action = ACTION_SLEEP;
        if (parts.size() > 1) q.duration = parts[1].toInt();
        return q;
    } else return q; // Неизвестная команда

    // Обработка пинов или слова ALL
    for (size_t i = 1; i < parts.size(); i++) {
        String p = parts[i];
        p.toUpperCase();
        if (p == "ALL") {
            q.pinMask = 0xFFFF;
            break;
        }
        int pinNum = p.toInt();
        if (pinNum >= 1 && pinNum <= 16) {
            q.pinMask |= (1 << (pinNum - 1));
        }
    }

    return q;
}

// Чтение действий из файла actions.bin
void DSLProcessor::runActionFromFile(int actionIdx) {
    File f = LittleFS.open("/actions.bin", "r");
    if (!f) {
        Serial.println("❌ Error: actions.bin not found");
        return;
    }

    int currentIdx = 0;
    while (f.available()) {
        uint8_t len = f.read(); // 1 байт длины
        if (currentIdx == actionIdx) {
            char* buf = (char*)malloc(len + 1);
            if (buf) {
                f.readBytes(buf, len);
                buf[len] = '\0';
                execute(String(buf)); // Создает новый поток исполнения
                free(buf);
            }
            break;
        } else {
            f.seek(f.position() + len);
        }
        currentIdx++;
    }
    f.close();
}

// Экстренная остановка всех сценариев
void DSLProcessor::stopAll() {
    _activeInstances.clear();
    _hw.updateOutputs();
    Serial.println("🛑 All DSL tasks stopped.");
}