#ifndef MONITOR_CONFIG_H
#define MONITOR_CONFIG_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#define MONITOR_CONFIG_FILE "/monitor.json"
#define MAX_DEVICES 8
#define MAX_FIELDS 10
#define MAX_SUBSCRIBED_TOPICS 8
#define DEFAULT_OFFLINE_TIMEOUT_SEC 20
#define MIN_OFFLINE_TIMEOUT_SEC 5
#define MAX_OFFLINE_TIMEOUT_SEC 300

// 顯示欄位類型
enum FieldType {
    FIELD_CPU_PERCENT = 0,
    FIELD_CPU_TEMP,
    FIELD_RAM_PERCENT,
    FIELD_GPU_PERCENT,
    FIELD_GPU_TEMP,
    FIELD_NET_RX,
    FIELD_NET_TX,
    FIELD_DISK_READ,
    FIELD_DISK_WRITE,
    FIELD_NONE = 255
};

// 設備設定
struct DeviceConfig {
    char hostname[32];      // 原始 hostname
    char alias[32];         // 顯示別名（顯示時由 ESP12 自行截斷）
    uint16_t displayTime;   // 顯示時間（秒）
    bool enabled;           // 是否啟用
};

// 閾值設定
struct ThresholdConfig {
    uint8_t cpuWarn;
    uint8_t cpuCrit;
    uint8_t ramWarn;
    uint8_t ramCrit;
    uint8_t gpuWarn;
    uint8_t gpuCrit;
    uint8_t tempWarn;
    uint8_t tempCrit;
};

// 版面欄位配置
struct FieldConfig {
    FieldType type;
    uint8_t row;        // 顯示行 (0-based)
    uint8_t size;       // 大小 (1=小, 2=大)
};

// 完整設定
struct MonitorConfig {
    // MQTT 設定
    char mqttServer[64];
    uint16_t mqttPort;
    char mqttUser[32];
    char mqttPass[32];
    char mqttTopic[64];
    char subscribedTopics[MAX_SUBSCRIBED_TOPICS][64];
    uint8_t subscribedTopicCount;

    // 設備設定
    DeviceConfig devices[MAX_DEVICES];
    uint8_t deviceCount;

    // 顯示設定
    FieldConfig fields[MAX_FIELDS];
    uint8_t fieldCount;

    // 閾值設定
    ThresholdConfig thresholds;

    // 輪播設定
    uint16_t defaultDisplayTime;  // 預設顯示時間（秒）
    bool autoCarousel;            // 自動輪播

    // 離線判定設定
    uint16_t offlineTimeoutSec;   // 幾秒無更新視為離線
};

class MonitorConfigManager {
public:
    MonitorConfig config;

    void begin() {
        setDefaults();
    }

    // 在主迴圈中呼叫，處理延遲儲存
    void loop() {
        if (_needsSave && millis() - _lastSaveTime > 5000) {
            save();
            _needsSave = false;
            _lastSaveTime = millis();
        }
    }

private:
    bool _needsSave = false;
    unsigned long _lastSaveTime = 0;

public:

    void markDirty() {
        _needsSave = true;
    }

    void setDefaults() {
        // MQTT 預設
        strcpy(config.mqttServer, "");
        config.mqttPort = 1883;
        strcpy(config.mqttUser, "");
        strcpy(config.mqttPass, "");
        strcpy(config.mqttTopic, "sys/agents/+/metrics");
        config.subscribedTopicCount = 0;

        // 設備預設
        config.deviceCount = 0;

        // 預設版面
        config.fieldCount = 5;
        config.fields[0] = {FIELD_CPU_PERCENT, 0, 2};
        config.fields[1] = {FIELD_CPU_TEMP, 0, 2};
        config.fields[2] = {FIELD_RAM_PERCENT, 1, 2};
        config.fields[3] = {FIELD_GPU_PERCENT, 2, 1};
        config.fields[4] = {FIELD_GPU_TEMP, 2, 1};

        // 閾值預設
        config.thresholds = {70, 90, 70, 90, 70, 90, 60, 80};

        // 輪播預設
        config.defaultDisplayTime = 5;
        config.autoCarousel = true;

        // 離線判定預設
        config.offlineTimeoutSec = DEFAULT_OFFLINE_TIMEOUT_SEC;
    }

    bool load() {
        if (!LittleFS.exists(MONITOR_CONFIG_FILE)) {
            Serial.println("Monitor config not found, using defaults");
            return false;
        }

        File file = LittleFS.open(MONITOR_CONFIG_FILE, "r");
        if (!file) {
            Serial.println("Failed to open monitor config");
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.println("JSON parse failed");
            return false;
        }

        // MQTT
        strlcpy(config.mqttServer, doc["mqtt"]["server"] | "", sizeof(config.mqttServer));
        config.mqttPort = doc["mqtt"]["port"] | 1883;
        strlcpy(config.mqttUser, doc["mqtt"]["user"] | "", sizeof(config.mqttUser));
        strlcpy(config.mqttPass, doc["mqtt"]["pass"] | "", sizeof(config.mqttPass));
        strlcpy(config.mqttTopic, doc["mqtt"]["topic"] | "sys/agents/+/metrics", sizeof(config.mqttTopic));
        config.subscribedTopicCount = 0;
        JsonArray topicsArr = doc["mqtt"]["subscribedTopics"].as<JsonArray>();
        if (!topicsArr.isNull()) {
            for (JsonVariant topicVar : topicsArr) {
                if (config.subscribedTopicCount >= MAX_SUBSCRIBED_TOPICS) break;
                const char* topic = topicVar | "";
                if (!topic || topic[0] == '\0') continue;
                strlcpy(config.subscribedTopics[config.subscribedTopicCount], topic,
                        sizeof(config.subscribedTopics[config.subscribedTopicCount]));
                config.subscribedTopicCount++;
            }
        }

        // 設備
        JsonArray devicesArr = doc["devices"].as<JsonArray>();
        config.deviceCount = 0;
        for (JsonObject dev : devicesArr) {
            if (config.deviceCount >= MAX_DEVICES) break;
            DeviceConfig& d = config.devices[config.deviceCount];
            strlcpy(d.hostname, dev["hostname"] | "", sizeof(d.hostname));
            strlcpy(d.alias, dev["alias"] | "", sizeof(d.alias));
            d.displayTime = dev["time"] | config.defaultDisplayTime;
            d.enabled = dev["enabled"] | true;
            config.deviceCount++;
        }

        // 閾值
        JsonObject th = doc["thresholds"];
        config.thresholds.cpuWarn = th["cpuWarn"] | 70;
        config.thresholds.cpuCrit = th["cpuCrit"] | 90;
        config.thresholds.ramWarn = th["ramWarn"] | 70;
        config.thresholds.ramCrit = th["ramCrit"] | 90;
        config.thresholds.gpuWarn = th["gpuWarn"] | 70;
        config.thresholds.gpuCrit = th["gpuCrit"] | 90;
        config.thresholds.tempWarn = th["tempWarn"] | 60;
        config.thresholds.tempCrit = th["tempCrit"] | 80;

        // 輪播
        config.defaultDisplayTime = doc["displayTime"] | 5;
        config.autoCarousel = doc["autoCarousel"] | true;
        config.offlineTimeoutSec = doc["offlineTimeoutSec"] | DEFAULT_OFFLINE_TIMEOUT_SEC;
        config.offlineTimeoutSec = constrain(config.offlineTimeoutSec,
                                            (uint16_t)MIN_OFFLINE_TIMEOUT_SEC,
                                            (uint16_t)MAX_OFFLINE_TIMEOUT_SEC);

        Serial.println("Monitor config loaded");
        Serial.printf("  deviceCount: %d\n", config.deviceCount);
        for (uint8_t i = 0; i < config.deviceCount; i++) {
            Serial.printf("  Device %d: hostname=%s, alias=%s, enabled=%d\n",
                i, config.devices[i].hostname, config.devices[i].alias, config.devices[i].enabled);
        }
        return true;
    }

    bool save() {
        JsonDocument doc;

        // MQTT
        doc["mqtt"]["server"] = config.mqttServer;
        doc["mqtt"]["port"] = config.mqttPort;
        doc["mqtt"]["user"] = config.mqttUser;
        doc["mqtt"]["pass"] = config.mqttPass;
        doc["mqtt"]["topic"] = config.mqttTopic;
        JsonArray subscribedTopics = doc["mqtt"]["subscribedTopics"].to<JsonArray>();
        for (uint8_t i = 0; i < config.subscribedTopicCount; i++) {
            subscribedTopics.add(config.subscribedTopics[i]);
        }

        // 設備
        JsonArray devicesArr = doc["devices"].to<JsonArray>();
        for (uint8_t i = 0; i < config.deviceCount; i++) {
            JsonObject dev = devicesArr.add<JsonObject>();
            dev["hostname"] = config.devices[i].hostname;
            dev["alias"] = config.devices[i].alias;
            dev["time"] = config.devices[i].displayTime;
            dev["enabled"] = config.devices[i].enabled;
        }

        // 閾值
        doc["thresholds"]["cpuWarn"] = config.thresholds.cpuWarn;
        doc["thresholds"]["cpuCrit"] = config.thresholds.cpuCrit;
        doc["thresholds"]["ramWarn"] = config.thresholds.ramWarn;
        doc["thresholds"]["ramCrit"] = config.thresholds.ramCrit;
        doc["thresholds"]["gpuWarn"] = config.thresholds.gpuWarn;
        doc["thresholds"]["gpuCrit"] = config.thresholds.gpuCrit;
        doc["thresholds"]["tempWarn"] = config.thresholds.tempWarn;
        doc["thresholds"]["tempCrit"] = config.thresholds.tempCrit;

        // 輪播
        doc["displayTime"] = config.defaultDisplayTime;
        doc["autoCarousel"] = config.autoCarousel;
        doc["offlineTimeoutSec"] = config.offlineTimeoutSec;

        File file = LittleFS.open(MONITOR_CONFIG_FILE, "w");
        if (!file) {
            Serial.println("Failed to write monitor config");
            return false;
        }

        serializeJson(doc, file);
        file.close();

        Serial.println("Monitor config saved");
        return true;
    }

    // 取得或建立設備設定（自動新增新設備）
    DeviceConfig* getOrCreateDevice(const char* hostname) {
        // 先找現有的
        for (uint8_t i = 0; i < config.deviceCount; i++) {
            if (strcmp(config.devices[i].hostname, hostname) == 0) {
                return &config.devices[i];
            }
        }

        // 新增設備
        if (config.deviceCount < MAX_DEVICES) {
            DeviceConfig& d = config.devices[config.deviceCount];
            strlcpy(d.hostname, hostname, sizeof(d.hostname));

            // 預設別名：使用完整 hostname，顯示時由 ESP12 自行截斷
            strlcpy(d.alias, hostname, sizeof(d.alias));

            d.displayTime = config.defaultDisplayTime;
            d.enabled = false;
            config.deviceCount++;
            _needsSave = true;  // 標記需要儲存，稍後在主迴圈中儲存

            return &d;
        }

        return nullptr;
    }
};

#endif
