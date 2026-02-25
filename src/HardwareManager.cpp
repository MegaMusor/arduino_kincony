#include "HardwareManager.h"

HardwareManager::HardwareManager() : _initialized(false), _ethStarted(false) {
    for(int i=0; i<16; i++) _timers[i].active = false;
    _i2cMutex = xSemaphoreCreateMutex();
}

void HardwareManager::init(JsonDocument& config) {
    if (_initialized) return;

    pinMode(46, OUTPUT);
    digitalWrite(46, HIGH);
    delay(500);

    int sda = config["i2c_master"]["sda_io"] | 9;
    int scl = config["i2c_master"]["scl_io"] | 10;
    Wire.begin(sda, scl);
    Wire.setClock(400000);

    // Первичная очистка
    _portA = 0xFF; _portB = 0xFF;
    Wire.beginTransmission(0x24); Wire.write(0xFF); Wire.endTransmission();
    Wire.beginTransmission(0x25); Wire.write(0xFF); Wire.endTransmission();

    initEthernet(config);
    _initialized = true;
}

// Чтение для карт - теперь оно также сбрасывает "зависшие" команды записи
uint8_t HardwareManager::fastRead8(uint8_t address) {
    uint8_t data = 0xFF;
    if (xSemaphoreTake(_i2cMutex, 0)) {
        // Если есть отложенная запись для реле - выполняем её попутно
        if (_needUpdateA) {
            Wire.beginTransmission(0x24); Wire.write(_portA); 
            if(Wire.endTransmission() == 0) _needUpdateA = false;
        }
        if (_needUpdateB) {
            Wire.beginTransmission(0x25); Wire.write(_portB); 
            if(Wire.endTransmission() == 0) _needUpdateB = false;
        }

        // Читаем вход (карту)
        Wire.requestFrom(address, (uint8_t)1);
        if (Wire.available()) data = Wire.read();
        
        xSemaphoreGive(_i2cMutex);
    }
    return data;
}

void HardwareManager::digitalWritePCF(uint8_t pin, bool state) {
    if (pin < 8) {
        if (state) _portA |= (1 << pin); else _portA &= ~(1 << pin);
        _needUpdateA = true;
    } else if (pin < 16) {
        uint8_t realPin = pin - 8;
        if (state) _portB |= (1 << realPin); else _portB &= ~(1 << realPin);
        _needUpdateB = true;
    }
}

void HardwareManager::pulsePCF(uint8_t pin, bool state, uint32_t durationMs) {
    if (pin >= 16) return;
    digitalWritePCF(pin, state);
    _timers[pin].pin = pin;
    _timers[pin].endTime = millis() + durationMs;
    _timers[pin].idleState = !state; 
    _timers[pin].active = true;
}

void HardwareManager::updateOutputs() {
    uint32_t now = millis();
    // 1. Проверка таймеров
    for (int i = 0; i < 16; i++) {
        if (_timers[i].active && now >= _timers[i].endTime) {
            digitalWritePCF(_timers[i].pin, _timers[i].idleState);
            _timers[i].active = false;
        }
    }

    if ((_needUpdateA || _needUpdateB) && xSemaphoreTake(_i2cMutex, 0)) {
        if (_needUpdateA) {
            Wire.beginTransmission(0x24); Wire.write(_portA);
            if(Wire.endTransmission() == 0) _needUpdateA = false;
        }
        if (_needUpdateB) {
            Wire.beginTransmission(0x25); Wire.write(_portB);
            if(Wire.endTransmission() == 0) _needUpdateB = false;
        }
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::initEthernet(JsonDocument& config) {
    const int eth_cs = 15;
    const int eth_reset = 1;
    pinMode(eth_reset, OUTPUT);
    digitalWrite(eth_reset, LOW);
    delay(200); 
    digitalWrite(eth_reset, HIGH);
    delay(1000); 

    SPI.begin(42, 44, 43);
    Ethernet.init(eth_cs);

    uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
    bool useStatic = config["network"]["use_static"] | false;

    if (!useStatic) {
        if (Ethernet.begin(mac) == 0) useStatic = true;
    }

    if (useStatic) {
        IPAddress ip, gw, mask, dns;
        ip.fromString(config["network"]["ip_address"] | "10.199.100.212");
        gw.fromString(config["network"]["gateway"] | "10.199.100.1");
        mask.fromString(config["network"]["subnet"] | "255.255.255.0");
        dns.fromString(config["network"]["dns"] | "8.8.8.8");
        Ethernet.begin(mac, ip, dns, gw, mask);
    }
    _ethStarted = true;
    Serial.print("✅ Ethernet Ready! IP: ");
    Serial.println(Ethernet.localIP());
}

bool HardwareManager::saveConfig(JsonDocument& config) {
    File file = LittleFS.open("/config.json", "w");
    if (!file) return false;
    serializeJson(config, file);
    file.close();
    return true;
}