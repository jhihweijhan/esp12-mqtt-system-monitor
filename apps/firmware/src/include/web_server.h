#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>

#include "connection_policy.h"
#include "device_store.h"
#include "html_monitor.h"
#include "html_page.h"
#include "monitor_config.h"
#include "mqtt_transport.h"
#include "wifi_manager.h"

class WebServerManager {
public:
    explicit WebServerManager(WiFiManager& wifiMgr) : _server(80), _wifiMgr(wifiMgr) {}

    void setMonitorConfig(MonitorConfigManager* config) {
        _monitorConfig = config;
    }

    void setMQTTTransport(MQTTTransport* mqtt) {
        _mqtt = mqtt;
    }

    void setDeviceStore(DeviceStore* store) {
        _store = store;
    }

    void loop() {
        processPendingWifiApply();

        if (_pendingRestart && millis() >= _restartAt) {
            Serial.println("Restarting...");
            delay(100);
            ESP.restart();
        }
    }

    void begin() {
        static bool headersInitialized = false;
        if (!headersInitialized) {
            DefaultHeaders::Instance().addHeader("Cache-Control", "no-store");
            DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
            DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");
            DefaultHeaders::Instance().addHeader("Referrer-Policy", "no-referrer");
            headersInitialized = true;
        }

        _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (_wifiMgr.isAPMode) {
                request->send_P(200, "text/html", HTML_PAGE);
            } else {
                request->redirect("/monitor");
            }
        });

        _server.on("/wifi", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (!_wifiMgr.isAPMode) {
                request->send(403, "application/json", "{\"success\":false,\"message\":\"available in AP mode only\"}");
                return;
            }
            request->send_P(200, "text/html", HTML_PAGE);
        });

        _server.on("/monitor", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send_P(200, "text/html", (const uint8_t*)HTML_MONITOR, HTML_MONITOR_LEN);
        });

        _server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
            if (!_wifiMgr.isAPMode) {
                request->send(403, "application/json", "{\"success\":false,\"message\":\"available in AP mode only\"}");
                return;
            }
            String json = _wifiMgr.getScanResults();
            request->send(200, "application/json", json);
        });

        _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest* request) {
            handleWifiSave(request);
        });

        _server.on("/api/v2/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
            sendConfig(request);
        });
        _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
            sendConfig(request);
        });

        AsyncCallbackJsonWebHandler* configHandlerV2 =
            new AsyncCallbackJsonWebHandler("/api/v2/config", [this](AsyncWebServerRequest* request, JsonVariant& json) {
                saveConfig(request, json);
            });
        _server.addHandler(configHandlerV2);

        AsyncCallbackJsonWebHandler* configHandlerLegacy =
            new AsyncCallbackJsonWebHandler("/api/config", [this](AsyncWebServerRequest* request, JsonVariant& json) {
                saveConfig(request, json);
            });
        _server.addHandler(configHandlerLegacy);

        _server.on("/api/v2/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
            sendStatus(request);
        });
        _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
            sendStatus(request);
        });

        _server.begin();
        Serial.println("Web Server started");
    }

private:
    enum WifiApplyState : uint8_t {
        WIFI_APPLY_IDLE = 0,
        WIFI_APPLY_PENDING_START,
        WIFI_APPLY_CONNECTING,
        WIFI_APPLY_RETRY_WAIT,
        WIFI_APPLY_FAILED,
        WIFI_APPLY_SUCCESS
    };

    AsyncWebServer _server;
    WiFiManager& _wifiMgr;
    MonitorConfigManager* _monitorConfig = nullptr;
    MQTTTransport* _mqtt = nullptr;
    DeviceStore* _store = nullptr;
    volatile bool _pendingRestart = false;
    unsigned long _restartAt = 0;
    WifiApplyState _wifiApplyState = WIFI_APPLY_IDLE;
    unsigned long _wifiApplyNextAt = 0;
    uint8_t _wifiApplyAttempts = 0;

    bool isWifiApplyBusy() const {
        return _wifiApplyState == WIFI_APPLY_PENDING_START || _wifiApplyState == WIFI_APPLY_CONNECTING ||
               _wifiApplyState == WIFI_APPLY_RETRY_WAIT;
    }

    const char* wifiApplyStateToString() const {
        switch (_wifiApplyState) {
            case WIFI_APPLY_PENDING_START:
                return "pending";
            case WIFI_APPLY_CONNECTING:
                return "connecting";
            case WIFI_APPLY_RETRY_WAIT:
                return "retry_wait";
            case WIFI_APPLY_FAILED:
                return "failed";
            case WIFI_APPLY_SUCCESS:
                return "success";
            default:
                return "idle";
        }
    }

    void handleWifiSave(AsyncWebServerRequest* request) {
        if (!_wifiMgr.isAPMode) {
            request->send(403, "application/json", "{\"success\":false,\"message\":\"available in AP mode only\"}");
            return;
        }

        if (isWifiApplyBusy()) {
            request->send(409, "application/json", "{\"success\":false,\"message\":\"wifi apply in progress\"}");
            return;
        }

        String ssid = "";
        String pass = "";

        if (request->hasParam("ssid", true)) {
            ssid = request->getParam("ssid", true)->value();
        }
        if (request->hasParam("pass", true)) {
            pass = request->getParam("pass", true)->value();
        }

        if (ssid.length() == 0) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID required\"}");
            return;
        }

        if (!isValidWifiCredentialLength(ssid.length(), pass.length())) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid WiFi credential length\"}");
            return;
        }

        if (!_wifiMgr.saveConfig(ssid, pass)) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"save failed\"}");
            return;
        }

        _wifiApplyAttempts = 0;
        _wifiApplyState = WIFI_APPLY_PENDING_START;
        _wifiApplyNextAt = millis();

        request->send(202, "application/json", "{\"success\":true,\"message\":\"connecting\",\"pending\":true}");
    }

    void sendConfig(AsyncWebServerRequest* request) {
        if (!_monitorConfig) {
            request->send(500, "application/json", "{\"error\":\"config not available\"}");
            return;
        }

        JsonDocument doc;
        MonitorConfig& cfg = _monitorConfig->config;

        doc["version"] = 2;
        doc["mqtt"]["server"] = cfg.mqttServer;
        doc["mqtt"]["port"] = cfg.mqttPort;
        doc["mqtt"]["topic"] = cfg.mqttTopic;
        doc["mqtt"]["user"] = cfg.mqttUser;

        JsonArray subscribedTopics = doc["mqtt"]["subscribedTopics"].to<JsonArray>();
        for (uint8_t i = 0; i < cfg.subscribedTopicCount; i++) {
            subscribedTopics.add(cfg.subscribedTopics[i]);
        }

        JsonArray availableTopics = doc["mqtt"]["availableTopics"].to<JsonArray>();
        char knownHosts[MAX_DEVICES + MAX_DEVICES][32];
        uint8_t knownHostCount = 0;

        auto addKnownHost = [&](const char* hostname) {
            if (!hostname || hostname[0] == '\0') {
                return;
            }
            for (uint8_t i = 0; i < knownHostCount; i++) {
                if (strcmp(knownHosts[i], hostname) == 0) {
                    return;
                }
            }
            if (knownHostCount >= (MAX_DEVICES + MAX_DEVICES)) {
                return;
            }
            strlcpy(knownHosts[knownHostCount], hostname, sizeof(knownHosts[knownHostCount]));
            knownHostCount++;
        };

        for (uint8_t i = 0; i < cfg.deviceCount; i++) {
            addKnownHost(cfg.devices[i].hostname);
        }

        if (_store) {
            for (uint8_t i = 0; i < MAX_DEVICES; i++) {
                DeviceSlot* slot = _store->getByIndex(i);
                if (slot) {
                    addKnownHost(slot->hostname);
                }
            }
        }

        for (uint8_t i = 0; i < knownHostCount; i++) {
            char topicBuf[96];
            snprintf(topicBuf, sizeof(topicBuf), "sys/agents/%s/metrics/v2", knownHosts[i]);
            availableTopics.add(topicBuf);
        }

        JsonArray devices = doc["devices"].to<JsonArray>();
        for (uint8_t i = 0; i < cfg.deviceCount; i++) {
            JsonObject dev = devices.add<JsonObject>();
            dev["hostname"] = cfg.devices[i].hostname;
            dev["alias"] = cfg.devices[i].alias;
            dev["time"] = cfg.devices[i].displayTime;
            dev["enabled"] = cfg.devices[i].enabled;
        }

        doc["thresholds"]["cpuWarn"] = cfg.thresholds.cpuWarn;
        doc["thresholds"]["cpuCrit"] = cfg.thresholds.cpuCrit;
        doc["thresholds"]["ramWarn"] = cfg.thresholds.ramWarn;
        doc["thresholds"]["ramCrit"] = cfg.thresholds.ramCrit;
        doc["thresholds"]["gpuWarn"] = cfg.thresholds.gpuWarn;
        doc["thresholds"]["gpuCrit"] = cfg.thresholds.gpuCrit;
        doc["thresholds"]["tempWarn"] = cfg.thresholds.tempWarn;
        doc["thresholds"]["tempCrit"] = cfg.thresholds.tempCrit;

        doc["displayTime"] = cfg.defaultDisplayTime;
        doc["autoCarousel"] = cfg.autoCarousel;
        doc["offlineTimeoutSec"] = cfg.offlineTimeoutSec;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    }

    void saveConfig(AsyncWebServerRequest* request, JsonVariant& json) {
        if (!_monitorConfig) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"config not available\"}");
            return;
        }

        if (request->contentLength() > MQTT_MAX_PAYLOAD_BYTES) {
            request->send(413, "application/json", "{\"success\":false,\"message\":\"payload too large\"}");
            return;
        }

        JsonObject data = json.as<JsonObject>();
        MonitorConfig& cfg = _monitorConfig->config;

        if (data["mqtt"].is<JsonObject>()) {
            JsonObject mqtt = data["mqtt"];
            const char* mqttServer = mqtt["server"] | "";
            const char* mqttTopic = mqtt["topic"] | "sys/agents/+/metrics/v2";
            const char* mqttUser = mqtt["user"] | "";
            uint16_t mqttPort = mqtt["port"] | 1883;

            if (strlen(mqttServer) >= sizeof(cfg.mqttServer) || strlen(mqttTopic) >= sizeof(cfg.mqttTopic) ||
                strlen(mqttUser) >= sizeof(cfg.mqttUser) || !isValidMqttPort(mqttPort)) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid MQTT settings\"}");
                return;
            }

            strlcpy(cfg.mqttServer, mqttServer, sizeof(cfg.mqttServer));
            cfg.mqttPort = mqttPort;
            strlcpy(cfg.mqttTopic, mqttTopic, sizeof(cfg.mqttTopic));
            strlcpy(cfg.mqttUser, mqttUser, sizeof(cfg.mqttUser));

            const char* pass = mqtt["pass"] | "";
            if (strlen(pass) > 0) {
                strlcpy(cfg.mqttPass, pass, sizeof(cfg.mqttPass));
            }

            cfg.subscribedTopicCount = 0;
            if (mqtt["subscribedTopics"].is<JsonArray>()) {
                JsonArray topics = mqtt["subscribedTopics"].as<JsonArray>();
                for (JsonVariant topicVar : topics) {
                    if (cfg.subscribedTopicCount >= MAX_SUBSCRIBED_TOPICS) {
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"too many subscribed topics\"}");
                        return;
                    }

                    const char* topicValue = topicVar | "";
                    if (!isValidSenderMetricsTopic(topicValue)) {
                        request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid sender topic\"}");
                        return;
                    }

                    bool duplicate = false;
                    for (uint8_t i = 0; i < cfg.subscribedTopicCount; i++) {
                        if (strcmp(cfg.subscribedTopics[i], topicValue) == 0) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        continue;
                    }

                    strlcpy(cfg.subscribedTopics[cfg.subscribedTopicCount], topicValue,
                            sizeof(cfg.subscribedTopics[cfg.subscribedTopicCount]));
                    cfg.subscribedTopicCount++;
                }
            }
        }

        if (data["devices"].is<JsonArray>()) {
            JsonArray devices = data["devices"].as<JsonArray>();
            cfg.deviceCount = 0;
            for (JsonObject dev : devices) {
                if (cfg.deviceCount >= MAX_DEVICES) {
                    break;
                }
                DeviceConfig& d = cfg.devices[cfg.deviceCount];
                strlcpy(d.hostname, dev["hostname"] | "", sizeof(d.hostname));
                strlcpy(d.alias, dev["alias"] | "", sizeof(d.alias));
                d.displayTime = dev["time"] | cfg.defaultDisplayTime;
                d.enabled = dev["enabled"] | true;
                cfg.deviceCount++;
            }
        }

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

        cfg.defaultDisplayTime = data["displayTime"] | 5;
        cfg.autoCarousel = data["autoCarousel"] | true;
        cfg.offlineTimeoutSec = data["offlineTimeoutSec"] | DEFAULT_OFFLINE_TIMEOUT_SEC;
        cfg.offlineTimeoutSec = constrain(cfg.offlineTimeoutSec, (uint16_t)MIN_OFFLINE_TIMEOUT_SEC,
                                          (uint16_t)MAX_OFFLINE_TIMEOUT_SEC);

        if (_monitorConfig->save()) {
            request->send(200, "application/json", "{\"success\":true}");
            _pendingRestart = true;
            _restartAt = millis() + 3000;
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"save failed\"}");
        }
    }

    void sendStatus(AsyncWebServerRequest* request) {
        JsonDocument doc;

        doc["mqttConnected"] = _mqtt ? _mqtt->isConnected() : false;
        doc["deviceCount"] = _store ? _store->deviceCount : 0;
        doc["onlineCount"] = (_store && _monitorConfig) ? _store->getOnlineCount(_monitorConfig) : 0;
        doc["wifiApplyState"] = wifiApplyStateToString();

        if (_store) {
            JsonArray devices = doc["devices"].to<JsonArray>();
            for (uint8_t i = 0; i < MAX_DEVICES; i++) {
                DeviceSlot* slot = _store->getByIndex(i);
                if (!slot) {
                    continue;
                }

                JsonObject dev = devices.add<JsonObject>();
                dev["hostname"] = slot->hostname;
                dev["online"] = slot->online;
                dev["cpu"] = roundedPercent(slot->frame.cpuPctX10);
                dev["ram"] = roundedPercent(slot->frame.ramPctX10);
            }
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    }

    void processPendingWifiApply() {
        if (!isWifiApplyBusy()) {
            return;
        }

        unsigned long now = millis();
        if (_wifiApplyState == WIFI_APPLY_RETRY_WAIT) {
            if ((long)(now - _wifiApplyNextAt) < 0) {
                return;
            }
            _wifiApplyState = WIFI_APPLY_PENDING_START;
        }

        if (_wifiApplyState == WIFI_APPLY_PENDING_START) {
            _wifiApplyAttempts++;
            if (!_wifiMgr.startConnectWiFi()) {
                _wifiApplyState = WIFI_APPLY_FAILED;
                Serial.println("WiFi apply failed: unable to start connect");
                return;
            }
            _wifiApplyState = WIFI_APPLY_CONNECTING;
            return;
        }

        if (_wifiApplyState != WIFI_APPLY_CONNECTING) {
            return;
        }

        WiFiManager::ConnectResult result = _wifiMgr.pollConnect();
        if (result == WiFiManager::CONNECT_IN_PROGRESS || result == WiFiManager::CONNECT_IDLE) {
            return;
        }

        if (result == WiFiManager::CONNECT_SUCCESS) {
            _wifiApplyState = WIFI_APPLY_SUCCESS;
            _pendingRestart = true;
            _restartAt = millis() + 3000;
            Serial.println("WiFi apply success, scheduling restart...");
            return;
        }

        if (_wifiApplyAttempts < 2) {
            _wifiApplyState = WIFI_APPLY_RETRY_WAIT;
            _wifiApplyNextAt = millis() + 1000;
            return;
        }

        _wifiApplyState = WIFI_APPLY_FAILED;
        Serial.println("WiFi apply failed after retries");
        _wifiMgr.startAP();
        _wifiMgr.startScan();
    }
};

#endif
