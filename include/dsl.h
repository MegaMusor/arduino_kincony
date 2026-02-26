#ifndef DSL_H
#define DSL_H

#include <Arduino.h>
#include "HardwareManager.h"
#include <vector>
#include <deque> 

enum DSLAction { ACTION_NONE = 0, ACTION_OPEN, ACTION_CLOSE, ACTION_SLEEP };

// Единая структура для команд
struct QueuedCommand {
    DSLAction action = ACTION_NONE;
    uint16_t pinMask = 0;
    uint32_t duration = 0;
};

class DSLProcessor {
public:
    DSLProcessor(HardwareManager& hw);
    void runActionFromFile(int actionIdx);
    void clearQueue();
    void begin();
    void execute(String line);   
    void tick();                 
  

private:
    HardwareManager& _hw;
    std::deque<QueuedCommand> _queue; 
    unsigned long _nextStepTime = 0;   
    bool _isWaiting = false;          

    QueuedCommand parse(String subLine); // Возвращает QueuedCommand
    void applyImmediate(QueuedCommand cmd);
};

#endif