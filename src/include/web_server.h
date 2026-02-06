#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include "wifi_manager.h"
#include "html_page.h"
#include "html_monitor.h"
#include "monitor_config.h"
#include "mqtt_client.h"

class WebServerManager {
public:
    WebServerManager(WiFiManager& wifiMgr) : _server(80), _wifiMgr(wifiMgr) {}

    void setMonitorConfig(MonitorConfigManager* config) { _monitorConfig = config; }
    void setMQTTClient(MQTTClient* mqtt) { _mqtt = mqtt; }

    // 在 main loop 中呼叫，處理延遲重啟
    void loop() {
        if (_pendingRestart && millis() >= _restartAt) {
            Serial.println("Restarting...");
            LittleFS.end();  // 確保檔案系統已 flush
            delay(100);
            ESP.restart();
        }
    }

    void begin() {
        // 首頁 - 根據模式顯示不同頁面
        _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
            if (_wifiMgr.isAPMode) {
                // AP 模式：顯示 WiFi 設定頁面
                request->send_P(200, "text/html", HTML_PAGE);
            } else {
                // 已連線：重導向到監控 dashboard
                request->redirect("/monitor");
            }
        });

        // WiFi 設定頁面（直接存取）
        _server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send_P(200, "text/html", HTML_PAGE);
        });

        // 監控 Dashboard (使用 chunked response with pre-calculated length)
        _server.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *request) {
            AsyncWebServerResponse *response = request->beginChunkedResponse("text/html",
                [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                    // Use pre-calculated length (HTML_MONITOR_LEN) to avoid slow strlen_P()
                    if (index >= HTML_MONITOR_LEN) return 0;
                    size_t remaining = HTML_MONITOR_LEN - index;
                    size_t toSend = (remaining > maxLen) ? maxLen : remaining;
                    memcpy_P(buffer, HTML_MONITOR + index, toSend);
                    return toSend;
                });
            request->send(response);
        });

        // 掃描 WiFi
        _server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String json = _wifiMgr.getScanResults();
            request->send(200, "application/json", json);
        });

        // 儲存 WiFi 設定
        _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
            String ssid = "";
            String pass = "";

            if (request->hasParam("ssid", true)) {
                ssid = request->getParam("ssid", true)->value();
            }
            if (request->hasParam("pass", true)) {
                pass = request->getParam("pass", true)->value();
            }

            Serial.printf("WiFi config: SSID=%s\n", ssid.c_str());

            // 儲存設定
            _wifiMgr.saveConfig(ssid, pass);

            // 嘗試連線
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(ssid.c_str(), pass.c_str());

            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
                delay(500);
            }

            String response;
            if (WiFi.status() == WL_CONNECTED) {
                String ip = WiFi.localIP().toString();
                response = "{\"success\":true,\"ip\":\"" + ip + "\"}";
                request->send(200, "application/json", response);

                _pendingRestart = true;
                _restartAt = millis() + 1000;
            } else {
                response = "{\"success\":false,\"message\":\"Connection failed\"}";
                request->send(200, "application/json", response);
            }
        });

        // 取得監控設定
        _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
            if (!_monitorConfig) {
                request->send(500, "application/json", "{\"error\":\"config not available\"}");
                return;
            }

            JsonDocument doc;
            MonitorConfig& cfg = _monitorConfig->config;

            // MQTT
            doc["mqtt"]["server"] = cfg.mqttServer;
            doc["mqtt"]["port"] = cfg.mqttPort;
            doc["mqtt"]["topic"] = cfg.mqttTopic;
            doc["mqtt"]["user"] = cfg.mqttUser;
            // 不回傳密碼

            // 設備
            JsonArray devices = doc["devices"].to<JsonArray>();
            for (uint8_t i = 0; i < cfg.deviceCount; i++) {
                JsonObject dev = devices.add<JsonObject>();
                dev["hostname"] = cfg.devices[i].hostname;
                dev["alias"] = cfg.devices[i].alias;
                dev["time"] = cfg.devices[i].displayTime;
                dev["enabled"] = cfg.devices[i].enabled;
            }

            // 閾值
            doc["thresholds"]["cpuWarn"] = cfg.thresholds.cpuWarn;
            doc["thresholds"]["cpuCrit"] = cfg.thresholds.cpuCrit;
            doc["thresholds"]["ramWarn"] = cfg.thresholds.ramWarn;
            doc["thresholds"]["ramCrit"] = cfg.thresholds.ramCrit;
            doc["thresholds"]["gpuWarn"] = cfg.thresholds.gpuWarn;
            doc["thresholds"]["gpuCrit"] = cfg.thresholds.gpuCrit;
            doc["thresholds"]["tempWarn"] = cfg.thresholds.tempWarn;
            doc["thresholds"]["tempCrit"] = cfg.thresholds.tempCrit;

            // 輪播
            doc["displayTime"] = cfg.defaultDisplayTime;
            doc["autoCarousel"] = cfg.autoCarousel;

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        });

        // 儲存監控設定
        AsyncCallbackJsonWebHandler* configHandler = new AsyncCallbackJsonWebHandler("/api/config",
            [this](AsyncWebServerRequest *request, JsonVariant &json) {
                if (!_monitorConfig) {
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"config not available\"}");
                    return;
                }

                JsonObject data = json.as<JsonObject>();
                MonitorConfig& cfg = _monitorConfig->config;

                // MQTT
                if (data["mqtt"].is<JsonObject>()) {
                    JsonObject mqtt = data["mqtt"];
                    strlcpy(cfg.mqttServer, mqtt["server"] | "", sizeof(cfg.mqttServer));
                    cfg.mqttPort = mqtt["port"] | 1883;
                    strlcpy(cfg.mqttTopic, mqtt["topic"] | "sys/agents/+/metrics", sizeof(cfg.mqttTopic));
                    strlcpy(cfg.mqttUser, mqtt["user"] | "", sizeof(cfg.mqttUser));
                    const char* pass = mqtt["pass"] | "";
                    if (strlen(pass) > 0) {
                        strlcpy(cfg.mqttPass, pass, sizeof(cfg.mqttPass));
                    }
                }

                // 設備
                if (data["devices"].is<JsonArray>()) {
                    JsonArray devices = data["devices"].as<JsonArray>();
                    cfg.deviceCount = 0;
                    for (JsonObject dev : devices) {
                        if (cfg.deviceCount >= MAX_DEVICES) break;
                        DeviceConfig& d = cfg.devices[cfg.deviceCount];
                        strlcpy(d.hostname, dev["hostname"] | "", sizeof(d.hostname));
                        strlcpy(d.alias, dev["alias"] | "", sizeof(d.alias));
                        d.displayTime = dev["time"] | cfg.defaultDisplayTime;
                        d.enabled = dev["enabled"] | true;
                        cfg.deviceCount++;
                    }
                }

                // 閾值
                if (data["thresholds"].is<JsonObject>()) {
                    JsonObject th = data["thresholds"];
                    cfg.thresholds.cpuWarn = th["cpuWarn"] | 70;
                    cfg.thresholds.cpuCrit = th["cpuCrit"] | 90;
                    cfg.thresholds.ramWarn = th["ramWarn"] | 70;
                    cfg.thresholds.ramCrit = th["ramCrit"] | 90;
                    cfg.thresholds.gpuWarn = th["gpuWarn"] | 70;
                    cfg.thresholds.gpuCrit = th["gpuCrit"] | 90;
                    cfg.thresholds.tempWarn = th["tempWarn"] | 60;
                    cfg.thresholds.tempCrit = th["tempCrit"] | 80;
                }

                // 輪播
                cfg.defaultDisplayTime = data["displayTime"] | 5;
                cfg.autoCarousel = data["autoCarousel"] | true;

                // 儲存
                if (_monitorConfig->save()) {
                    request->send(200, "application/json", "{\"success\":true}");
                    Serial.println("Config saved, scheduling restart...");
                    _pendingRestart = true;
                    _restartAt = millis() + 1000;
                } else {
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"save failed\"}");
                }
            }
        );
        _server.addHandler(configHandler);

        // 取得即時狀態
        _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            JsonDocument doc;

            doc["mqttConnected"] = _mqtt ? _mqtt->isConnected() : false;
            doc["deviceCount"] = _mqtt ? _mqtt->deviceCount : 0;
            doc["onlineCount"] = _mqtt ? _mqtt->getOnlineCount() : 0;

            if (_mqtt) {
                JsonArray devices = doc["devices"].to<JsonArray>();
                for (uint8_t i = 0; i < _mqtt->deviceCount; i++) {
                    JsonObject dev = devices.add<JsonObject>();
                    dev["hostname"] = _mqtt->devices[i].hostname;
                    dev["online"] = _mqtt->devices[i].online;
                    dev["cpu"] = _mqtt->devices[i].cpuPercent;
                    dev["ram"] = _mqtt->devices[i].ramPercent;
                }
            }

            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        });

        _server.begin();
        Serial.println("Web Server started");
    }

private:
    AsyncWebServer _server;
    WiFiManager& _wifiMgr;
    MonitorConfigManager* _monitorConfig = nullptr;
    MQTTClient* _mqtt = nullptr;
    volatile bool _pendingRestart = false;
    unsigned long _restartAt = 0;
};

#endif
