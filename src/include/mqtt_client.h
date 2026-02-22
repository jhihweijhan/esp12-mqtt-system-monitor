#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "connection_policy.h"
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
    float gpuTemp;            // 主溫度 (edge/core)
    float gpuHotspotTemp;     // 熱點/junction 溫度
    float gpuMemTemp;         // VRAM 溫度
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
        // 限制到 8KB，兼顧大型 payload 與 ESP8266 記憶體壓力
        _client.setBufferSize(MQTT_MAX_PAYLOAD_BYTES);

        _reconnectFailureCount = 0;
        _nextReconnectAt = 0;
        reconnect();
    }

    void loop() {
        if (strlen(_configMgr->config.mqttServer) == 0) return;

        if (!_client.connected()) {
            unsigned long now = millis();
            connected = false;
            if (_nextReconnectAt == 0 || (long)(now - _nextReconnectAt) >= 0) {
                reconnect();
            }
        } else {
            _client.loop();
        }

        // 檢查設備離線狀態（依設定檔 timeout 秒數）
        unsigned long now = millis();
        unsigned long offlineTimeoutMs = getOfflineTimeoutMs();
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].online && (now - devices[i].lastUpdate > offlineTimeoutMs)) {
                devices[i].online = false;
                Serial.printf("Device offline: %s\n", devices[i].hostname);
            }
        }
    }

    bool isConnected() {
        return _client.connected();
    }

    bool isConnectedForDisplay() {
        unsigned long now = millis();
        return !shouldShowMqttDisconnectedStatus(_client.connected(),
                                                 now,
                                                 _lastConnectedAt,
                                                 _lastMessageAt);
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

    bool getDeviceConfigState(const char* hostname, bool& isKnown, bool& enabled) {
        isKnown = false;
        enabled = false;
        if (!_configMgr) return false;
        for (uint8_t i = 0; i < _configMgr->config.deviceCount; i++) {
            if (strcmp(_configMgr->config.devices[i].hostname, hostname) == 0) {
                isKnown = true;
                enabled = _configMgr->config.devices[i].enabled;
                return true;
            }
        }
        return false;
    }

    bool isTopicInAllowlist(const char* topic) const {
        if (!_configMgr || !topic) return false;
        for (uint8_t i = 0; i < _configMgr->config.subscribedTopicCount; i++) {
            if (strcmp(_configMgr->config.subscribedTopics[i], topic) == 0) {
                return true;
            }
        }
        return false;
    }

    void handleMessage(char* topic, byte* payload, unsigned int length) {
        if (!isValidMqttPayloadLength(length)) {
            Serial.printf("MQTT payload rejected: %u bytes\n", length);
            return;
        }

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

        bool isKnown = false;
        bool enabled = false;
        getDeviceConfigState(hostname, isKnown, enabled);

        if (_configMgr && isKnown && !enabled &&
            shouldAutoEnableDeviceOnSubscribedTopic(_configMgr->config.subscribedTopicCount) &&
            isTopicInAllowlist(topic)) {
            DeviceConfig* cfg = _configMgr->getOrCreateDevice(hostname);
            if (cfg && !cfg->enabled) {
                cfg->enabled = true;
                _configMgr->markDirty();
                enabled = true;
                Serial.printf("Auto-enable subscribed device: %s\n", hostname);
            }
        }

        if (_strictKnownHostsOnly && !isKnown) {
            return;
        }
        if (isKnown && !enabled) {
            return;
        }

        // 取得或建立設備
        DeviceMetrics* dev = getDevice(hostname);
        if (!dev) {
            if (deviceCount >= MAX_METRICS_DEVICES) {
                Serial.println("Max devices reached");
                return;
            }
            dev = &devices[deviceCount++];
            memset(dev, 0, sizeof(DeviceMetrics));
            strlcpy(dev->hostname, hostname, sizeof(dev->hostname));

            // 確保設定管理器也有此設備
            DeviceConfig* cfg = _configMgr->getOrCreateDevice(hostname);
            if (cfg &&
                shouldAutoEnableDeviceOnSubscribedTopic(_configMgr->config.subscribedTopicCount) &&
                !cfg->enabled) {
                cfg->enabled = true;
                _configMgr->markDirty();
            }
        }

        // 先標記該設備有收到訊息（heartbeat）
        // 即使 JSON 內容異常，也不應直接判定設備離線
        unsigned long nowMs = millis();
        dev->lastUpdate = nowMs;
        dev->online = true;
        _lastMessageAt = nowMs;

        // 解析 JSON (限制解析深度和大小以避免記憶體問題)
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
            Serial.printf("JSON parse failed [%s, %u bytes]: %s\n",
                          hostname, length, error.c_str());
            return;
        }
        yield();  // 讓 WDT 不會超時

        // CPU (agent_sender_async.py: cpu.percent_total)
        dev->cpuPercent = doc["cpu"]["percent_total"] | 0.0f;

        // RAM (agent_sender_async.py: memory.ram.percent/used/total)
        dev->ramPercent = doc["memory"]["ram"]["percent"] | 0.0f;
        // 將 bytes 轉換為 GB
        float ramUsedBytes = doc["memory"]["ram"]["used"] | 0.0f;
        float ramTotalBytes = doc["memory"]["ram"]["total"] | 0.0f;
        dev->ramUsedGB = ramUsedBytes / (1024.0f * 1024.0f * 1024.0f);
        dev->ramTotalGB = ramTotalBytes / (1024.0f * 1024.0f * 1024.0f);

        // GPU
        if (doc["gpu"].is<JsonObject>()) {
            dev->gpuPercent = doc["gpu"]["usage_percent"] | 0.0f;
            dev->gpuTemp = doc["gpu"]["temperature_celsius"] | 0.0f;
            dev->gpuHotspotTemp = 0.0f;
            dev->gpuMemTemp = 0.0f;

            // memory_percent: 直接讀取或從 used/total 計算
            dev->gpuMemPercent = doc["gpu"]["memory_percent"] | 0.0f;
            if (dev->gpuMemPercent == 0.0f) {
                float memUsed = doc["gpu"]["memory_used_mb"] | 0.0f;
                float memTotal = doc["gpu"]["memory_total_mb"] | 0.0f;
                if (memTotal > 0) {
                    dev->gpuMemPercent = (memUsed / memTotal) * 100.0f;
                }
            }

            // 解析 temperatures 陣列 (GPU/EDG, JCT/HSP, MEM/VRM)
            if (doc["gpu"]["temperatures"].is<JsonArray>()) {
                JsonArray temps = doc["gpu"]["temperatures"].as<JsonArray>();
                for (JsonVariant t : temps) {
                    const char* label = t["label"] | "";
                    float current = t["current"] | 0.0f;
                    if (strcmp(label, "GPU") == 0 || strcmp(label, "EDG") == 0 ||
                        strcmp(label, "COR") == 0) {
                        dev->gpuTemp = current;
                    } else if (strcmp(label, "JCT") == 0 || strcmp(label, "HSP") == 0) {
                        dev->gpuHotspotTemp = current;
                    } else if (strcmp(label, "MEM") == 0 || strcmp(label, "VRM") == 0) {
                        dev->gpuMemTemp = current;
                    }
                }
            }
        } else {
            dev->gpuPercent = 0.0f;
            dev->gpuTemp = 0.0f;
            dev->gpuHotspotTemp = 0.0f;
            dev->gpuMemTemp = 0.0f;
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

        _rxMessageCount++;
        unsigned long now = millis();
        if (now - _lastRxLogAt >= MQTT_RX_LOG_INTERVAL_MS) {
            Serial.printf("MQTT rx: %u msgs / %ums, last=%s\n",
                          _rxMessageCount,
                          (unsigned int)MQTT_RX_LOG_INTERVAL_MS,
                          hostname);
            _rxMessageCount = 0;
            _lastRxLogAt = now;
        }

        if (onMetricsReceived) {
            onMetricsReceived(hostname);
        }
    }

private:
    WiFiClient _wifiClient;
    PubSubClient _client;
    MonitorConfigManager* _configMgr;
    unsigned long _nextReconnectAt = 0;
    uint8_t _reconnectFailureCount = 0;
    bool _strictKnownHostsOnly = false;
    unsigned long _lastRxLogAt = 0;
    uint16_t _rxMessageCount = 0;
    unsigned long _lastConnectedAt = 0;
    unsigned long _lastMessageAt = 0;

    unsigned long getOfflineTimeoutMs() const {
        if (!_configMgr) return 30000;
        uint16_t sec = _configMgr->config.offlineTimeoutSec;
        if (sec < MIN_OFFLINE_TIMEOUT_SEC) sec = MIN_OFFLINE_TIMEOUT_SEC;
        if (sec > MAX_OFFLINE_TIMEOUT_SEC) sec = MAX_OFFLINE_TIMEOUT_SEC;
        return (unsigned long)sec * 1000UL;
    }

    static void mqttCallback(char* topic, byte* payload, unsigned int length) {
        if (_mqttInstance) {
            _mqttInstance->handleMessage(topic, payload, length);
        }
    }

    void subscribeConfiguredTopics() {
        _strictKnownHostsOnly = false;
        if (!_configMgr) return;

        char uniqueTopics[MAX_SUBSCRIBED_TOPICS][64];
        uint8_t uniqueCount = 0;

        for (uint8_t i = 0; i < _configMgr->config.subscribedTopicCount; i++) {
            const char* topic = _configMgr->config.subscribedTopics[i];
            if (!isValidSenderMetricsTopic(topic)) {
                Serial.printf("Skip invalid sender topic: %s\n", topic);
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < uniqueCount; j++) {
                if (strcmp(uniqueTopics[j], topic) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            if (uniqueCount >= MAX_SUBSCRIBED_TOPICS) {
                Serial.println("Skip sender topic: allowlist full");
                break;
            }
            strlcpy(uniqueTopics[uniqueCount], topic, sizeof(uniqueTopics[uniqueCount]));
            uniqueCount++;
        }

        if (!shouldSubscribeAnySenderTopic(uniqueCount)) {
            Serial.println("No sender topics configured, skip MQTT subscriptions");
            return;
        }

        for (uint8_t i = 0; i < uniqueCount; i++) {
            if (_client.subscribe(uniqueTopics[i])) {
                Serial.printf("Subscribed sender topic: %s\n", uniqueTopics[i]);
            } else {
                Serial.printf("Subscribe failed: %s\n", uniqueTopics[i]);
            }
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
            _reconnectFailureCount = 0;
            _nextReconnectAt = 0;
            _lastConnectedAt = millis();
            Serial.println("MQTT connected");
            subscribeConfiguredTopics();
        } else {
            connected = false;
            if (_reconnectFailureCount < 250) {
                _reconnectFailureCount++;
            }
            uint32_t delayMs = computeMqttReconnectDelayMs(_reconnectFailureCount);
            uint32_t jitter = random(0, 500);
            _nextReconnectAt = millis() + delayMs + jitter;
            Serial.printf("MQTT failed, rc=%d, retry in %lu ms\n",
                          _client.state(),
                          (unsigned long)(delayMs + jitter));
        }
    }
};

#endif
