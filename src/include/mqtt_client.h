#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "monitor_config.h"

#define MAX_METRICS_DEVICES 8

// 設備指標資料
struct DeviceMetrics {
    char hostname[32];
    uint32_t lastUpdate;      // millis() 時間戳
    bool online;

    // CPU
    float cpuPercent;
    float cpuTemp;

    // RAM
    float ramPercent;
    float ramUsedGB;
    float ramTotalGB;

    // GPU
    float gpuPercent;
    float gpuTemp;
    float gpuMemPercent;

    // 網路 (Mbps)
    float netRxMbps;
    float netTxMbps;

    // 磁碟 (MB/s)
    float diskReadMBs;
    float diskWriteMBs;
};

// 前向宣告以便回調使用
class MQTTClient;
static MQTTClient* _mqttInstance = nullptr;

class MQTTClient {
public:
    DeviceMetrics devices[MAX_METRICS_DEVICES];
    uint8_t deviceCount = 0;
    bool connected = false;

    typedef void (*MetricsCallback)(const char* hostname);
    MetricsCallback onMetricsReceived = nullptr;

    MQTTClient() : _client(_wifiClient) {
        _mqttInstance = this;
    }

    void begin(MonitorConfigManager& configMgr) {
        _configMgr = &configMgr;
    }

    void connect() {
        if (strlen(_configMgr->config.mqttServer) == 0) {
            Serial.println("MQTT server not configured");
            return;
        }

        _client.setServer(_configMgr->config.mqttServer, _configMgr->config.mqttPort);
        _client.setCallback(mqttCallback);
        // 增加緩衝區至 16KB 以處理大型 JSON 訊息
        // ESP8266 有 80KB RAM，這是合理的大小
        _client.setBufferSize(16384);

        reconnect();
    }

    void loop() {
        if (strlen(_configMgr->config.mqttServer) == 0) return;

        if (!_client.connected()) {
            unsigned long now = millis();
            if (now - _lastReconnect > 5000) {
                _lastReconnect = now;
                reconnect();
            }
        } else {
            _client.loop();
        }

        // 檢查設備離線狀態（30 秒無更新視為離線）
        unsigned long now = millis();
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].online && (now - devices[i].lastUpdate > 30000)) {
                devices[i].online = false;
                Serial.printf("Device offline: %s\n", devices[i].hostname);
            }
        }
    }

    bool isConnected() {
        return _client.connected();
    }

    DeviceMetrics* getDevice(const char* hostname) {
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (strcmp(devices[i].hostname, hostname) == 0) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    DeviceMetrics* getOnlineDevice(uint8_t index) {
        uint8_t count = 0;
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].online && isDeviceEnabled(devices[i].hostname)) {
                if (count == index) return &devices[i];
                count++;
            }
        }
        return nullptr;
    }

    uint8_t getOnlineCount() {
        uint8_t count = 0;
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].online && isDeviceEnabled(devices[i].hostname)) count++;
        }
        return count;
    }

    bool isDeviceEnabled(const char* hostname) {
        if (!_configMgr) return true;
        for (uint8_t i = 0; i < _configMgr->config.deviceCount; i++) {
            if (strcmp(_configMgr->config.devices[i].hostname, hostname) == 0) {
                return _configMgr->config.devices[i].enabled;
            }
        }
        return true;  // 預設啟用（新設備）
    }

    void handleMessage(char* topic, byte* payload, unsigned int length) {
        Serial.printf("MQTT msg: %s (%u bytes)\n", topic, length);

        // 解析 topic 取得 hostname: sys/agents/{hostname}/metrics
        // 找到第二個 '/' 後的內容（hostname）
        char* p = strchr(topic, '/');  // 第一個 /
        if (!p) { Serial.println("No 1st /"); return; }
        p = strchr(p + 1, '/');        // 第二個 /
        if (!p) { Serial.println("No 2nd /"); return; }
        char* start = p + 1;           // hostname 開始
        char* end = strchr(start, '/'); // 第三個 /
        if (!end) { Serial.println("No 3rd /"); return; }

        char hostname[32];
        size_t len = end - start;
        if (len >= sizeof(hostname)) len = sizeof(hostname) - 1;
        strncpy(hostname, start, len);
        hostname[len] = '\0';

        // 解析 JSON (限制解析深度和大小以避免記憶體問題)
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
            Serial.printf("JSON parse failed: %s\n", error.c_str());
            return;
        }
        yield();  // 讓 WDT 不會超時

        // 取得或建立設備
        DeviceMetrics* dev = getDevice(hostname);
        if (!dev) {
            if (deviceCount >= MAX_METRICS_DEVICES) {
                Serial.println("Max devices reached");
                return;
            }
            dev = &devices[deviceCount++];
            strlcpy(dev->hostname, hostname, sizeof(dev->hostname));

            // 確保設定管理器也有此設備
            _configMgr->getOrCreateDevice(hostname);
        }

        // 更新指標
        dev->lastUpdate = millis();
        dev->online = true;

        // CPU (agent_sender_async.py: cpu.percent_total)
        dev->cpuPercent = doc["cpu"]["percent_total"] | 0.0f;

        // RAM (agent_sender_async.py: memory.ram.percent/used/total)
        dev->ramPercent = doc["memory"]["ram"]["percent"] | 0.0f;
        // 將 bytes 轉換為 GB
        float ramUsedBytes = doc["memory"]["ram"]["used"] | 0.0f;
        float ramTotalBytes = doc["memory"]["ram"]["total"] | 0.0f;
        dev->ramUsedGB = ramUsedBytes / (1024.0f * 1024.0f * 1024.0f);
        dev->ramTotalGB = ramTotalBytes / (1024.0f * 1024.0f * 1024.0f);

        // GPU (agent_sender_async.py: gpu.usage_percent, gpu.temperature_celsius, gpu.memory_percent)
        if (doc["gpu"].is<JsonObject>()) {
            dev->gpuPercent = doc["gpu"]["usage_percent"] | 0.0f;
            dev->gpuTemp = doc["gpu"]["temperature_celsius"] | 0.0f;
            dev->gpuMemPercent = doc["gpu"]["memory_percent"] | 0.0f;
        } else {
            dev->gpuPercent = 0.0f;
            dev->gpuTemp = 0.0f;
            dev->gpuMemPercent = 0.0f;
        }

        // 網路 (agent_sender_async.py: network_io.total.rate.rx_bytes_per_s/tx_bytes_per_s)
        // 將 bytes/s 轉換為 Mbps (除以 1024*1024/8 = 131072)
        float netRxBps = doc["network_io"]["total"]["rate"]["rx_bytes_per_s"] | 0.0f;
        float netTxBps = doc["network_io"]["total"]["rate"]["tx_bytes_per_s"] | 0.0f;
        dev->netRxMbps = (netRxBps * 8.0f) / (1024.0f * 1024.0f);
        dev->netTxMbps = (netTxBps * 8.0f) / (1024.0f * 1024.0f);

        // 磁碟 (agent_sender_async.py: disk_io.{device}.rate.read_bytes_per_s/write_bytes_per_s)
        // 取所有磁碟的加總，轉換為 MB/s
        float totalReadBps = 0.0f;
        float totalWriteBps = 0.0f;
        JsonObject diskIo = doc["disk_io"].as<JsonObject>();
        for (JsonPair kv : diskIo) {
            JsonObject disk = kv.value().as<JsonObject>();
            if (disk["rate"].is<JsonObject>()) {
                totalReadBps += disk["rate"]["read_bytes_per_s"] | 0.0f;
                totalWriteBps += disk["rate"]["write_bytes_per_s"] | 0.0f;
            }
        }
        dev->diskReadMBs = totalReadBps / (1024.0f * 1024.0f);
        dev->diskWriteMBs = totalWriteBps / (1024.0f * 1024.0f);

        // CPU 溫度 (agent_sender_async.py: temperatures.k10temp[0].current 或 coretemp[0].current)
        dev->cpuTemp = 0.0f;
        if (doc["temperatures"].is<JsonObject>()) {
            JsonObject temps = doc["temperatures"].as<JsonObject>();
            // 只檢查常見的 CPU 溫度感測器，避免遍歷太多
            const char* cpuSensors[] = {"k10temp", "coretemp", "cpu_thermal"};
            for (int i = 0; i < 3; i++) {
                if (temps[cpuSensors[i]].is<JsonArray>()) {
                    JsonArray entries = temps[cpuSensors[i]].as<JsonArray>();
                    if (entries.size() > 0) {
                        dev->cpuTemp = entries[0]["current"] | 0.0f;
                        break;
                    }
                }
            }
        }
        yield();  // 讓 WDT 不會超時

        Serial.printf("Recv %s: CPU=%.0f%% RAM=%.0f%%\n",
                      hostname, dev->cpuPercent, dev->ramPercent);

        if (onMetricsReceived) {
            onMetricsReceived(hostname);
        }
    }

private:
    WiFiClient _wifiClient;
    PubSubClient _client;
    MonitorConfigManager* _configMgr;
    unsigned long _lastReconnect = 0;

    static void mqttCallback(char* topic, byte* payload, unsigned int length) {
        if (_mqttInstance) {
            _mqttInstance->handleMessage(topic, payload, length);
        }
    }

    void reconnect() {
        Serial.printf("Connecting MQTT: %s:%d\n",
                      _configMgr->config.mqttServer,
                      _configMgr->config.mqttPort);

        String clientId = "ESP12-" + String(random(0xffff), HEX);
        bool success;

        if (strlen(_configMgr->config.mqttUser) > 0) {
            success = _client.connect(clientId.c_str(),
                                      _configMgr->config.mqttUser,
                                      _configMgr->config.mqttPass);
        } else {
            success = _client.connect(clientId.c_str());
        }

        if (success) {
            connected = true;
            Serial.println("MQTT connected");
            _client.subscribe(_configMgr->config.mqttTopic);
            Serial.printf("Subscribed: %s\n", _configMgr->config.mqttTopic);
        } else {
            connected = false;
            Serial.printf("MQTT failed, rc=%d\n", _client.state());
        }
    }
};

#endif
