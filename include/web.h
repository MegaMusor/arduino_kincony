#ifndef WEB_H
#define WEB_H

#include <ArduinoJson.h>
#include <Ethernet.h>
#include "HardwareManager.h"

// Создаем "исправленный" класс сервера, который не будет абстрактным
class EspEthernetServer : public EthernetServer {
public:
    EspEthernetServer(uint16_t port) : EthernetServer(port) {}
    // Добавляем пустую реализацию того, что требует ESP32
    void begin(uint16_t port) override { (void)port; begin(); }
    using EthernetServer::begin; 
};

class WebHandler {
public:
    WebHandler(JsonDocument& config, HardwareManager& hw);
    void begin();
    void handle();

private:
    JsonDocument& _config;
    HardwareManager& _hw;
    EspEthernetServer* _server; // Используем наш исправленный класс

    void processClient(EthernetClient& client);
    void sendHtmlPage(EthernetClient& client);
};

#endif