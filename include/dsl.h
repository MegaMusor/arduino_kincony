#ifndef DSL_H
#define DSL_H

#include <Arduino.h>
#include <LittleFS.h>
#include <deque>
#include <vector>
#include <list> // Для хранения списка активных процессов
#include "HardwareManager.h"

enum DSLAction { ACTION_NONE = 0, ACTION_OPEN, ACTION_CLOSE, ACTION_SLEEP };

struct QueuedCommand {
    DSLAction action = ACTION_NONE;
    uint16_t pinMask = 0;
    uint32_t duration = 0;
};

// Состояние одного запущенного сценария
struct DSLInstance {
    std::deque<QueuedCommand> commands;
    unsigned long nextStepTime = 0;
    bool isWaiting = false;
};

class DSLProcessor {
public:
    DSLProcessor(HardwareManager& hw);
    void begin();
    
    // Запускает НОВЫЙ параллельный процесс
    void execute(String line); 
    void runActionFromFile(int actionIdx);
    
    // Обновляет ВСЕ запущенные процессы
    void tick(); 
    
    // Остановить вообще всё
    void stopAll();

private:
    HardwareManager& _hw;
    std::list<DSLInstance> _activeInstances; // Список всех параллельных сценариев

    QueuedCommand parseSubCommand(String sub);
};

#endif