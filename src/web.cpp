#include "web.h"
#include <base64.h>
#include <time.h>
#include <sys/time.h>

WebHandler::WebHandler(JsonDocument& config, HardwareManager& hw) 
    : _config(config), _hw(hw) {
    _server = new EspEthernetServer(80); 
}

void WebHandler::begin() {
    _server->begin();
}

void WebHandler::handle() {
    EthernetClient client = _server->available();
    if (client) processClient(client);
}

void WebHandler::processClient(EthernetClient& client) {
    String header = "";
    String postData = "";
    bool readingBody = false;
    int contentLength = 0;

    unsigned long timeout = millis();
    while (client.connected() && (millis() - timeout < 2000)) {
        if (client.available()) {
            char c = client.read();
            if (!readingBody) {
                header += c;
                if (header.endsWith("\r\n\r\n")) {
                    if (header.startsWith("POST")) {
                        readingBody = true;
                        int pos = header.indexOf("Content-Length: ");
                        if (pos != -1) contentLength = header.substring(pos + 16).toInt();
                    } else break;
                }
            } else {
                postData += c;
                if (postData.length() >= contentLength) break;
            }
        }
    }

    String authUser = _config["system"]["web_admin"]["login"] | "admin";
    String authPass = _config["system"]["web_admin"]["password"] | "smart20241";
    String authExpected = base64::encode(authUser + ":" + authPass);
    if (header.indexOf("Authorization: Basic " + authExpected) == -1) {
        client.println("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"A16\"\r\n\r\n");
        client.stop(); return;
    }

    if (header.startsWith("POST")) {
        auto getParam = [&](String name) {
            int idx = postData.indexOf(name + "=");
            if (idx == -1) return String("");
            int endIdx = postData.indexOf("&", idx);
            String val = postData.substring(idx + name.length() + 1, endIdx == -1 ? postData.length() : endIdx);
            val.replace("+", " "); val.replace("%2E", "."); val.replace("%3A", ":");
            return val;
        };

        _config["network"]["use_static"] = (getParam("ip_mode") == "static");
        _config["network"]["ip_address"] = getParam("ip");
        _config["network"]["subnet"] = getParam("mask");
        _config["network"]["gateway"] = getParam("gw");
        _config["network"]["dns"] = getParam("dns1");

        _config["ntp"]["use_ntp"] = (postData.indexOf("use_ntp=on") != -1);
        _config["ntp"]["ntp_server"] = getParam("ntp_srv");
        
        String rtcTime = getParam("manual_time");
        if (rtcTime.length() > 10) {
            struct tm tm;
            int y, m, d, hh, mm;
            if (sscanf(rtcTime.c_str(), "%d-%d-%dT%d:%d", &y, &m, &d, &hh, &mm) == 5) {
                tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
                tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = 0;
                time_t t = mktime(&tm);
                struct timeval now_tv = { .tv_sec = t };
                settimeofday(&now_tv, NULL); 
            }
        }

        _config["server_connection"]["server_ip"] = getParam("srv_ip");
        _config["server_connection"]["server_port"] = getParam("srv_port").toInt();
        String newPwd = getParam("pwd");
        if (newPwd.length() > 0) _config["system"]["web_admin"]["password"] = newPwd;

        _hw.saveConfig(_config);

        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
        client.println("<html><body><h1>Saved!</h1><script>setTimeout(()=>location.href='/',2000);</script></body></html>");
        client.stop();
        delay(500);
        ESP.restart();
    } else {
        sendHtmlPage(client);
    }
}

void WebHandler::sendHtmlPage(EthernetClient& client) {
    client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nConnection: close\r\n\r\n");
    
    time_t now; time(&now);
    struct tm ti; localtime_r(&now, &ti);
    char curTime[25]; strftime(curTime, sizeof(curTime), "%d.%m.%Y %H:%M", &ti);

    IPAddress currentIp = Ethernet.localIP();
    byte mac[6]; Ethernet.MACAddress(mac);
    char macStr[20]; sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    bool isStatic = _config["network"]["use_static"] | false;

    // Отправляем частями, чтобы не перегружать память
    client.print("<!DOCTYPE html><html><head><meta charset='UTF-8'><style>");
    client.print("body{font-family:sans-serif;background:#f0f2f5;padding:20px;}");
    client.print(".box{max-width:500px;margin:auto;background:#fff;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}");
    client.print("section{border:1px solid #ddd;padding:15px;margin-bottom:15px;border-radius:5px;}");
    client.print("label{display:block;font-weight:bold;margin-top:10px;}");
    client.print("input,select{width:100%;padding:8px;box-sizing:border-box;margin-top:5px;}");
    client.print(".info-bar{background:#e9ecef;padding:12px;border-radius:5px;margin-bottom:15px;font-size:13px;color:#444;}");
    client.print("</style></head><body><div class='box'><h2>Kincony A16 Config</h2>");
    
    client.print("<div class='info-bar'><strong>Текущий IP:</strong> " + currentIp.toString() + "<br>");
    client.print("<strong>MAC-адрес:</strong> " + String(macStr) + "<br>");
    client.print("<strong>Системное время:</strong> " + String(curTime) + "</div>");

    client.print("<form method='POST'><section><h3>1. Сетевые настройки</h3><label>Режим IP</label>");
    client.print("<select name='ip_mode'>");
    client.print("<option value='static'" + String(isStatic ? " selected" : "") + ">Статический IP</option>");
    client.print("<option value='dhcp'" + String(!isStatic ? " selected" : "") + ">DHCP</option></select>");
    
    client.print("<label>IP адрес (для Static)</label><input name='ip' value='" + _config["network"]["ip_address"].as<String>() + "'>");
    client.print("<label>Маска подсети</label><input name='mask' value='" + _config["network"]["subnet"].as<String>() + "'>");
    client.print("<label>Шлюз</label><input name='gw' value='" + _config["network"]["gateway"].as<String>() + "'>");
    client.print("<label>DNS сервер</label><input name='dns1' value='" + _config["network"]["dns"].as<String>() + "'></section>");

    client.print("<section><h3>2. Дата и время</h3>");
    client.print("<label><input type='checkbox' name='use_ntp' " + String(_config["ntp"]["use_ntp"] ? "checked" : "") + " style='width:auto;'> Включить NTP</label>");
    client.print("<label>NTP Сервер</label><input name='ntp_srv' value='" + _config["ntp"]["ntp_server"].as<String>() + "'>");
    client.print("<label>Установить время вручную</label><input type='datetime-local' name='manual_time'></section>");

    client.print("<section><h3>3. Сервер и Доступ</h3>");
    client.print("<label>IP Сервера СКУД</label><input name='srv_ip' value='" + _config["server_connection"]["server_ip"].as<String>() + "'>");
    client.print("<label>Порт сервера</label><input name='srv_port' type='number' value='" + _config["server_connection"]["server_port"].as<String>() + "'>");
    client.print("<label>Сменить пароль входа</label><input name='pwd' type='password' placeholder='Оставьте пустым'></section>");

    client.print("<button type='submit' style='width:100%;padding:15px;background:#28a745;color:#fff;border:none;border-radius:5px;font-weight:bold;cursor:pointer;'>СОХРАНИТЬ И ПЕРЕЗАГРУЗИТЬ</button>");
    client.print("</form></div></body></html>");
    client.stop();
}