# ESP12 MQTT 系統監控器實作計畫

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在 ESP12 240x240 TFT 螢幕上顯示多台設備的系統監控資訊（CPU/RAM/GPU/網路/磁碟），透過 MQTT 訂閱接收數據，並提供 WebUI 設定介面。

**Architecture:**
- ESP12 連接 MQTT broker 訂閱 `hwmonitor/+/metrics` topic
- 多設備自動輪播顯示，每台設備顯示時間可設定
- WebUI 提供視覺化設定介面（拖拉排版 + 預設模板）
- 設定儲存於 LittleFS，包含：設備別名、顯示欄位、輪播時間、警示閾值

**Tech Stack:** ESP8266, PubSubClient (MQTT), ArduinoJson, ESPAsyncWebServer, LittleFS

---

## 檔案結構

```
src/
├── main.cpp                    # 主程式（修改）
└── include/
    ├── tft_driver.h            # TFT 驅動（現有）
    ├── font_8x16.h             # 字型（現有）
    ├── qr_display.h            # QR Code（現有）
    ├── wifi_manager.h          # WiFi 管理（現有）
    ├── web_server.h            # Web 伺服器（修改）
    ├── html_page.h             # WiFi 設定頁（現有）
    ├── mqtt_client.h           # MQTT 客戶端（新增）
    ├── monitor_config.h        # 監控設定管理（新增）
    ├── monitor_display.h       # 監控顯示邏輯（新增）
    ├── html_monitor.h          # 監控設定 WebUI（新增）
    └── ui_components.h         # UI 元件（進度條等）（新增）
```

---

## Task 1: 新增 MQTT 依賴

**Files:**
- Modify: `platformio.ini`

**Step 1: 新增 PubSubClient 依賴**

在 `platformio.ini` 的 `lib_deps` 加入 MQTT 函式庫：

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    ricmoo/QRCode@^0.0.1
    https://github.com/me-no-dev/ESPAsyncTCP.git
    https://github.com/me-no-dev/ESPAsyncWebServer.git
    knolleary/PubSubClient@^2.8
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add platformio.ini
git commit -m "feat: add PubSubClient MQTT library dependency"
```

---

## Task 2: 建立 UI 元件庫

**Files:**
- Create: `src/include/ui_components.h`

**Step 1: 建立 UI 元件檔案**

```cpp
#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <Arduino.h>
#include "tft_driver.h"

// 根據數值取得顏色（綠→黃→紅）
inline uint16_t getValueColor(int value, int warnThreshold = 70, int critThreshold = 90) {
    if (value >= critThreshold) return COLOR_RED;
    if (value >= warnThreshold) return COLOR_YELLOW;
    return COLOR_GREEN;
}

// 根據溫度取得顏色
inline uint16_t getTempColor(int temp, int warnThreshold = 60, int critThreshold = 80) {
    if (temp >= critThreshold) return COLOR_RED;
    if (temp >= warnThreshold) return COLOR_YELLOW;
    return COLOR_CYAN;
}

class UIComponents {
public:
    UIComponents(TFTDriver& tft) : _tft(tft) {}

    // 繪製水平進度條
    void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                         int percent, uint16_t color, uint16_t bgColor = COLOR_GRAY) {
        // 背景
        _tft.fillRect(x, y, w, h, bgColor);
        // 進度
        int16_t fillW = (w * percent) / 100;
        if (fillW > 0) {
            _tft.fillRect(x, y, fillW, h, color);
        }
    }

    // 繪製帶標籤的進度條
    void drawLabeledBar(int16_t x, int16_t y, const char* label,
                        int percent, int16_t barWidth = 100) {
        // 標籤 (4 字元寬)
        _tft.drawString(x, y, label, COLOR_WHITE, COLOR_BLACK, 1);

        // 進度條
        int16_t barX = x + 40;  // 標籤後
        uint16_t color = getValueColor(percent);
        drawProgressBar(barX, y + 2, barWidth, 12, percent, color);

        // 百分比數字
        char buf[8];
        snprintf(buf, sizeof(buf), "%3d%%", percent);
        _tft.drawString(barX + barWidth + 4, y, buf, color, COLOR_BLACK, 1);
    }

    // 繪製大數字（用於主要指標）
    void drawBigValue(int16_t x, int16_t y, const char* label,
                      int value, const char* unit, uint16_t color) {
        // 標籤
        _tft.drawString(x, y, label, COLOR_GRAY, COLOR_BLACK, 1);

        // 大數字
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%s", value, unit);
        _tft.drawString(x, y + 18, buf, color, COLOR_BLACK, 2);
    }

    // 繪製雙欄數值（如 CPU 87% 62°C）
    void drawDualValue(int16_t x, int16_t y, const char* label,
                       int val1, const char* unit1, int val2, const char* unit2) {
        // 標籤
        _tft.drawString(x, y, label, COLOR_WHITE, COLOR_BLACK, 2);

        // 第一個值
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%s", val1, unit1);
        uint16_t color1 = getValueColor(val1);
        _tft.drawString(x + 64, y, buf, color1, COLOR_BLACK, 2);

        // 第二個值（溫度）
        snprintf(buf, sizeof(buf), "%d%s", val2, unit2);
        uint16_t color2 = getTempColor(val2);
        _tft.drawString(x + 140, y, buf, color2, COLOR_BLACK, 2);
    }

    // 繪製小型資訊行
    void drawInfoLine(int16_t x, int16_t y, const char* label, const char* value) {
        _tft.drawString(x, y, label, COLOR_GRAY, COLOR_BLACK, 1);
        _tft.drawString(x + 40, y, value, COLOR_WHITE, COLOR_BLACK, 1);
    }

    // 繪製網路流量
    void drawNetworkIO(int16_t x, int16_t y, float rxMbps, float txMbps) {
        char buf[32];
        snprintf(buf, sizeof(buf), "NET  %.1fM  %.1fM", rxMbps, txMbps);

        // 繪製箭頭符號
        _tft.drawString(x, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        _tft.drawString(x + 32, y, "\x19", COLOR_GREEN, COLOR_BLACK, 1);  // 下箭頭
        snprintf(buf, sizeof(buf), "%.1fM", rxMbps);
        _tft.drawString(x + 40, y, buf, COLOR_GREEN, COLOR_BLACK, 1);

        _tft.drawString(x + 96, y, "\x18", COLOR_CYAN, COLOR_BLACK, 1);   // 上箭頭
        snprintf(buf, sizeof(buf), "%.1fM", txMbps);
        _tft.drawString(x + 104, y, buf, COLOR_CYAN, COLOR_BLACK, 1);
    }

    // 繪製離線警告
    void drawOfflineAlert(int16_t y, const char* deviceName) {
        _tft.fillRect(0, y, TFT_WIDTH, 40, COLOR_RED);
        _tft.drawStringCentered(y + 4, "OFFLINE", COLOR_WHITE, COLOR_RED, 2);
        _tft.drawStringCentered(y + 24, deviceName, COLOR_WHITE, COLOR_RED, 1);
    }

    // 繪製設備標題列
    void drawDeviceHeader(const char* name, bool isOnline = true) {
        uint16_t bgColor = isOnline ? 0x1082 : COLOR_RED;  // 深藍或紅色
        _tft.fillRect(0, 0, TFT_WIDTH, 28, bgColor);
        _tft.drawStringCentered(6, name, COLOR_WHITE, bgColor, 2);
    }

private:
    TFTDriver& _tft;
};

#endif
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/include/ui_components.h
git commit -m "feat: add UI components library with progress bars and value displays"
```

---

## Task 3: 建立監控設定管理

**Files:**
- Create: `src/include/monitor_config.h`

**Step 1: 建立設定管理類別**

```cpp
#ifndef MONITOR_CONFIG_H
#define MONITOR_CONFIG_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#define MONITOR_CONFIG_FILE "/monitor.json"
#define MAX_DEVICES 8
#define MAX_FIELDS 10

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
    char alias[8];          // 顯示別名（最多 4 中文字或 7 英文）
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
};

class MonitorConfigManager {
public:
    MonitorConfig config;

    void begin() {
        setDefaults();
    }

    void setDefaults() {
        // MQTT 預設
        strcpy(config.mqttServer, "");
        config.mqttPort = 1883;
        strcpy(config.mqttUser, "");
        strcpy(config.mqttPass, "");
        strcpy(config.mqttTopic, "hwmonitor/+/metrics");

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
    }

    bool load() {
        if (!LittleFS.exists(MONITOR_CONFIG_FILE)) {
            Serial.println("監控設定檔不存在，使用預設值");
            return false;
        }

        File file = LittleFS.open(MONITOR_CONFIG_FILE, "r");
        if (!file) {
            Serial.println("無法開啟監控設定檔");
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.println("JSON 解析失敗");
            return false;
        }

        // MQTT
        strlcpy(config.mqttServer, doc["mqtt"]["server"] | "", sizeof(config.mqttServer));
        config.mqttPort = doc["mqtt"]["port"] | 1883;
        strlcpy(config.mqttUser, doc["mqtt"]["user"] | "", sizeof(config.mqttUser));
        strlcpy(config.mqttPass, doc["mqtt"]["pass"] | "", sizeof(config.mqttPass));
        strlcpy(config.mqttTopic, doc["mqtt"]["topic"] | "hwmonitor/+/metrics", sizeof(config.mqttTopic));

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

        Serial.println("監控設定已載入");
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

        File file = LittleFS.open(MONITOR_CONFIG_FILE, "w");
        if (!file) {
            Serial.println("無法寫入監控設定檔");
            return false;
        }

        serializeJson(doc, file);
        file.close();

        Serial.println("監控設定已儲存");
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

            // 預設別名：取 hostname 前 4 個字元
            size_t len = strlen(hostname);
            if (len > 4) len = 4;
            strncpy(d.alias, hostname, len);
            d.alias[len] = '\0';

            d.displayTime = config.defaultDisplayTime;
            d.enabled = true;
            config.deviceCount++;

            save();  // 自動儲存
            return &d;
        }

        return nullptr;
    }
};

#endif
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/include/monitor_config.h
git commit -m "feat: add monitor configuration manager with device/threshold settings"
```

---

## Task 4: 建立 MQTT 客戶端

**Files:**
- Create: `src/include/mqtt_client.h`

**Step 1: 建立 MQTT 客戶端類別**

```cpp
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

class MQTTClient {
public:
    DeviceMetrics devices[MAX_METRICS_DEVICES];
    uint8_t deviceCount = 0;
    bool connected = false;

    typedef void (*MetricsCallback)(const char* hostname);
    MetricsCallback onMetricsReceived = nullptr;

    MQTTClient() : _client(_wifiClient) {}

    void begin(MonitorConfigManager& configMgr) {
        _configMgr = &configMgr;
    }

    void connect() {
        if (strlen(_configMgr->config.mqttServer) == 0) {
            Serial.println("MQTT 伺服器未設定");
            return;
        }

        _client.setServer(_configMgr->config.mqttServer, _configMgr->config.mqttPort);
        _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
            this->handleMessage(topic, payload, length);
        });
        _client.setBufferSize(1024);  // 增加緩衝區

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
                Serial.printf("設備離線: %s\n", devices[i].hostname);
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
            if (devices[i].online) {
                if (count == index) return &devices[i];
                count++;
            }
        }
        return nullptr;
    }

    uint8_t getOnlineCount() {
        uint8_t count = 0;
        for (uint8_t i = 0; i < deviceCount; i++) {
            if (devices[i].online) count++;
        }
        return count;
    }

private:
    WiFiClient _wifiClient;
    PubSubClient _client;
    MonitorConfigManager* _configMgr;
    unsigned long _lastReconnect = 0;

    void reconnect() {
        Serial.printf("連接 MQTT: %s:%d\n",
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
            Serial.println("MQTT 已連接");
            _client.subscribe(_configMgr->config.mqttTopic);
            Serial.printf("已訂閱: %s\n", _configMgr->config.mqttTopic);
        } else {
            connected = false;
            Serial.printf("MQTT 連接失敗, rc=%d\n", _client.state());
        }
    }

    void handleMessage(char* topic, byte* payload, unsigned int length) {
        // 解析 topic 取得 hostname: hwmonitor/{hostname}/metrics
        char* start = strstr(topic, "/");
        if (!start) return;
        start++;
        char* end = strstr(start, "/");
        if (!end) return;

        char hostname[32];
        size_t len = end - start;
        if (len >= sizeof(hostname)) len = sizeof(hostname) - 1;
        strncpy(hostname, start, len);
        hostname[len] = '\0';

        // 解析 JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
            Serial.printf("JSON 解析失敗: %s\n", error.c_str());
            return;
        }

        // 取得或建立設備
        DeviceMetrics* dev = getDevice(hostname);
        if (!dev) {
            if (deviceCount >= MAX_METRICS_DEVICES) {
                Serial.println("設備數量已達上限");
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

        // CPU
        dev->cpuPercent = doc["cpu"]["percent"] | 0.0f;
        dev->cpuTemp = doc["cpu"]["temp"] | 0.0f;

        // RAM
        dev->ramPercent = doc["ram"]["percent"] | 0.0f;
        dev->ramUsedGB = doc["ram"]["used_gb"] | 0.0f;
        dev->ramTotalGB = doc["ram"]["total_gb"] | 0.0f;

        // GPU（可能不存在）
        if (doc.containsKey("gpu")) {
            dev->gpuPercent = doc["gpu"]["percent"] | 0.0f;
            dev->gpuTemp = doc["gpu"]["temp"] | 0.0f;
            dev->gpuMemPercent = doc["gpu"]["mem_percent"] | 0.0f;
        }

        // 網路
        dev->netRxMbps = doc["net"]["rx_mbps"] | 0.0f;
        dev->netTxMbps = doc["net"]["tx_mbps"] | 0.0f;

        // 磁碟
        dev->diskReadMBs = doc["disk"]["read_mbs"] | 0.0f;
        dev->diskWriteMBs = doc["disk"]["write_mbs"] | 0.0f;

        Serial.printf("收到 %s: CPU=%.0f%% RAM=%.0f%%\n",
                      hostname, dev->cpuPercent, dev->ramPercent);

        if (onMetricsReceived) {
            onMetricsReceived(hostname);
        }
    }
};

#endif
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/include/mqtt_client.h
git commit -m "feat: add MQTT client with device metrics parsing"
```

---

## Task 5: 建立監控顯示邏輯

**Files:**
- Create: `src/include/monitor_display.h`

**Step 1: 建立監控顯示類別**

```cpp
#ifndef MONITOR_DISPLAY_H
#define MONITOR_DISPLAY_H

#include <Arduino.h>
#include "tft_driver.h"
#include "ui_components.h"
#include "mqtt_client.h"
#include "monitor_config.h"

class MonitorDisplay {
public:
    MonitorDisplay(TFTDriver& tft, MQTTClient& mqtt, MonitorConfigManager& config)
        : _tft(tft), _ui(tft), _mqtt(mqtt), _config(config) {}

    void begin() {
        _currentDevice = 0;
        _lastSwitch = millis();
    }

    void loop() {
        unsigned long now = millis();

        // 自動輪播
        if (_config.config.autoCarousel && _mqtt.getOnlineCount() > 1) {
            DeviceMetrics* dev = _mqtt.getOnlineDevice(_currentDevice);
            uint16_t displayTime = _config.config.defaultDisplayTime;

            // 取得此設備的自訂顯示時間
            if (dev) {
                DeviceConfig* devCfg = _config.getOrCreateDevice(dev->hostname);
                if (devCfg) {
                    displayTime = devCfg->displayTime;
                }
            }

            if (now - _lastSwitch > displayTime * 1000) {
                nextDevice();
            }
        }

        // 每 500ms 更新顯示
        if (now - _lastRefresh > 500) {
            _lastRefresh = now;
            refresh();
        }
    }

    void nextDevice() {
        uint8_t onlineCount = _mqtt.getOnlineCount();
        if (onlineCount == 0) return;

        _currentDevice = (_currentDevice + 1) % onlineCount;
        _lastSwitch = millis();
        _forceRedraw = true;
    }

    void prevDevice() {
        uint8_t onlineCount = _mqtt.getOnlineCount();
        if (onlineCount == 0) return;

        if (_currentDevice == 0) {
            _currentDevice = onlineCount - 1;
        } else {
            _currentDevice--;
        }
        _lastSwitch = millis();
        _forceRedraw = true;
    }

    void refresh() {
        // 檢查是否有離線設備需要警示
        bool hasOffline = false;
        const char* offlineDevice = nullptr;
        for (uint8_t i = 0; i < _mqtt.deviceCount; i++) {
            DeviceConfig* cfg = _config.getOrCreateDevice(_mqtt.devices[i].hostname);
            if (cfg && cfg->enabled && !_mqtt.devices[i].online) {
                hasOffline = true;
                offlineDevice = _mqtt.devices[i].hostname;
                break;
            }
        }

        // 顯示離線警示（閃爍）
        if (hasOffline && ((millis() / 1000) % 4 < 2)) {
            showOfflineAlert(offlineDevice);
            return;
        }

        // 取得當前設備
        DeviceMetrics* dev = _mqtt.getOnlineDevice(_currentDevice);
        if (!dev) {
            showNoDevice();
            return;
        }

        // 顯示設備資訊
        showDevice(dev);
    }

private:
    TFTDriver& _tft;
    UIComponents _ui;
    MQTTClient& _mqtt;
    MonitorConfigManager& _config;

    uint8_t _currentDevice = 0;
    unsigned long _lastSwitch = 0;
    unsigned long _lastRefresh = 0;
    bool _forceRedraw = true;
    char _lastHostname[32] = "";

    void showDevice(DeviceMetrics* dev) {
        // 檢查是否需要完整重繪
        if (_forceRedraw || strcmp(_lastHostname, dev->hostname) != 0) {
            _tft.fillScreen(COLOR_BLACK);
            strcpy(_lastHostname, dev->hostname);
            _forceRedraw = false;
        }

        // 取得設備別名
        DeviceConfig* cfg = _config.getOrCreateDevice(dev->hostname);
        const char* alias = cfg ? cfg->alias : dev->hostname;

        // 標題列
        _ui.drawDeviceHeader(alias, true);

        // 線上設備數量指示
        uint8_t onlineCount = _mqtt.getOnlineCount();
        if (onlineCount > 1) {
            char indicator[16];
            snprintf(indicator, sizeof(indicator), "%d/%d", _currentDevice + 1, onlineCount);
            _tft.drawString(200, 8, indicator, COLOR_GRAY, 0x1082, 1);
        }

        // === 主要區域（預設版面） ===
        int16_t y = 36;

        // CPU + 溫度（大字）
        ThresholdConfig& th = _config.config.thresholds;

        _tft.drawString(8, y, "CPU", COLOR_WHITE, COLOR_BLACK, 2);

        char buf[16];
        int cpuPct = (int)dev->cpuPercent;
        snprintf(buf, sizeof(buf), "%3d%%", cpuPct);
        uint16_t cpuColor = (cpuPct >= th.cpuCrit) ? COLOR_RED :
                            (cpuPct >= th.cpuWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.drawString(64, y, buf, cpuColor, COLOR_BLACK, 2);

        int cpuTemp = (int)dev->cpuTemp;
        snprintf(buf, sizeof(buf), "%2dC", cpuTemp);
        uint16_t tempColor = (cpuTemp >= th.tempCrit) ? COLOR_RED :
                             (cpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
        _tft.drawString(152, y, buf, tempColor, COLOR_BLACK, 2);

        y += 32;

        // RAM（大字）
        _tft.drawString(8, y, "RAM", COLOR_WHITE, COLOR_BLACK, 2);

        int ramPct = (int)dev->ramPercent;
        snprintf(buf, sizeof(buf), "%3d%%", ramPct);
        uint16_t ramColor = (ramPct >= th.ramCrit) ? COLOR_RED :
                            (ramPct >= th.ramWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.drawString(64, y, buf, ramColor, COLOR_BLACK, 2);

        // RAM 用量
        snprintf(buf, sizeof(buf), "%.1f/%.0fG", dev->ramUsedGB, dev->ramTotalGB);
        _tft.drawString(136, y, buf, COLOR_GRAY, COLOR_BLACK, 1);

        y += 32;

        // GPU（如果有）
        if (dev->gpuPercent > 0 || dev->gpuTemp > 0) {
            _tft.drawString(8, y, "GPU", COLOR_WHITE, COLOR_BLACK, 1);

            int gpuPct = (int)dev->gpuPercent;
            snprintf(buf, sizeof(buf), "%3d%%", gpuPct);
            uint16_t gpuColor = (gpuPct >= th.gpuCrit) ? COLOR_RED :
                                (gpuPct >= th.gpuWarn) ? COLOR_YELLOW : COLOR_GREEN;
            _tft.drawString(40, y, buf, gpuColor, COLOR_BLACK, 1);

            int gpuTemp = (int)dev->gpuTemp;
            snprintf(buf, sizeof(buf), "%2dC", gpuTemp);
            uint16_t gTempColor = (gpuTemp >= th.tempCrit) ? COLOR_RED :
                                  (gpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
            _tft.drawString(88, y, buf, gTempColor, COLOR_BLACK, 1);

            snprintf(buf, sizeof(buf), "Mem:%d%%", (int)dev->gpuMemPercent);
            _tft.drawString(136, y, buf, COLOR_GRAY, COLOR_BLACK, 1);

            y += 20;
        }

        // 網路
        _tft.drawString(8, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "v%.1fM", dev->netRxMbps);
        _tft.drawString(40, y, buf, COLOR_GREEN, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "^%.1fM", dev->netTxMbps);
        _tft.drawString(112, y, buf, COLOR_CYAN, COLOR_BLACK, 1);

        y += 20;

        // 磁碟
        _tft.drawString(8, y, "DISK", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "R:%.1fM", dev->diskReadMBs);
        _tft.drawString(48, y, buf, COLOR_WHITE, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "W:%.1fM", dev->diskWriteMBs);
        _tft.drawString(128, y, buf, COLOR_WHITE, COLOR_BLACK, 1);

        // 底部狀態列
        y = 220;
        if (_mqtt.isConnected()) {
            _tft.drawString(8, y, "MQTT OK", COLOR_GREEN, COLOR_BLACK, 1);
        } else {
            _tft.drawString(8, y, "MQTT --", COLOR_RED, COLOR_BLACK, 1);
        }

        // 更新時間
        unsigned long age = (millis() - dev->lastUpdate) / 1000;
        snprintf(buf, sizeof(buf), "%lus ago", age);
        _tft.drawString(160, y, buf, COLOR_GRAY, COLOR_BLACK, 1);
    }

    void showNoDevice() {
        if (!_forceRedraw) return;
        _forceRedraw = false;

        _tft.fillScreen(COLOR_BLACK);
        _ui.drawDeviceHeader("Monitor", true);

        _tft.drawStringCentered(100, "Waiting", COLOR_CYAN, COLOR_BLACK, 2);
        _tft.drawStringCentered(130, "for data...", COLOR_GRAY, COLOR_BLACK, 1);

        if (!_mqtt.isConnected()) {
            _tft.drawStringCentered(180, "MQTT not connected", COLOR_RED, COLOR_BLACK, 1);
        }
    }

    void showOfflineAlert(const char* hostname) {
        _tft.fillScreen(COLOR_BLACK);

        _tft.fillRect(0, 80, TFT_WIDTH, 80, COLOR_RED);
        _tft.drawStringCentered(90, "OFFLINE", COLOR_WHITE, COLOR_RED, 2);

        DeviceConfig* cfg = _config.getOrCreateDevice(hostname);
        const char* alias = cfg ? cfg->alias : hostname;
        _tft.drawStringCentered(120, alias, COLOR_WHITE, COLOR_RED, 2);

        _forceRedraw = true;
    }
};

#endif
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/include/monitor_display.h
git commit -m "feat: add monitor display with auto carousel and offline alerts"
```

---

## Task 6: 建立監控設定 WebUI

**Files:**
- Create: `src/include/html_monitor.h`

**Step 1: 建立 WebUI HTML**

```cpp
#ifndef HTML_MONITOR_H
#define HTML_MONITOR_H

const char HTML_MONITOR[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP12 Monitor 設定</title>
    <style>
        * { box-sizing: border-box; font-family: Arial, sans-serif; }
        body { margin: 0; padding: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 600px; margin: 0 auto; }
        h1 { text-align: center; color: #00d9ff; font-size: 24px; }
        h2 { color: #aaa; font-size: 18px; border-bottom: 1px solid #333; padding-bottom: 8px; }

        .card { background: #16213e; border-radius: 8px; padding: 16px; margin-bottom: 16px; }
        .form-group { margin-bottom: 16px; }
        label { display: block; margin-bottom: 6px; color: #aaa; font-size: 14px; }
        input, select { width: 100%; padding: 10px; font-size: 16px;
            border: 2px solid #333; border-radius: 6px;
            background: #0f1729; color: #fff; }
        input:focus, select:focus { outline: none; border-color: #00d9ff; }

        .row { display: flex; gap: 12px; }
        .row > * { flex: 1; }

        button { padding: 12px 24px; font-size: 16px; font-weight: bold;
            border: none; border-radius: 6px; cursor: pointer;
            background: linear-gradient(135deg, #00d9ff, #0095ff);
            color: #fff; transition: transform 0.1s; }
        button:active { transform: scale(0.98); }
        button.secondary { background: #333; }

        .device-list { margin-top: 12px; }
        .device-item { display: flex; align-items: center; gap: 12px;
            background: #0f1729; padding: 12px; border-radius: 6px; margin-bottom: 8px; }
        .device-item .name { flex: 1; }
        .device-item .alias { width: 80px; }
        .device-item .time { width: 60px; }
        .device-item input { margin: 0; }

        .status { padding: 8px 12px; border-radius: 4px; margin-top: 12px; display: none; }
        .status.success { display: block; background: #0a3d2a; color: #00ff88; }
        .status.error { display: block; background: #3d0a0a; color: #ff4444; }

        .threshold-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; }
        .threshold-item label { margin-bottom: 4px; }

        .tabs { display: flex; gap: 4px; margin-bottom: 16px; }
        .tab { flex: 1; padding: 12px; text-align: center; background: #0f1729;
            border-radius: 6px 6px 0 0; cursor: pointer; color: #aaa; }
        .tab.active { background: #16213e; color: #00d9ff; }
        .tab-content { display: none; }
        .tab-content.active { display: block; }

        .online { color: #00ff88; }
        .offline { color: #ff4444; }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚡ Monitor 設定</h1>

        <div class="tabs">
            <div class="tab active" onclick="showTab('mqtt')">MQTT</div>
            <div class="tab" onclick="showTab('devices')">設備</div>
            <div class="tab" onclick="showTab('display')">顯示</div>
        </div>

        <!-- MQTT 設定 -->
        <div id="tab-mqtt" class="tab-content active">
            <div class="card">
                <h2>MQTT 連線</h2>
                <div class="form-group">
                    <label>伺服器</label>
                    <input type="text" id="mqttServer" placeholder="例: 192.168.1.100">
                </div>
                <div class="row">
                    <div class="form-group">
                        <label>連接埠</label>
                        <input type="number" id="mqttPort" value="1883">
                    </div>
                    <div class="form-group">
                        <label>Topic</label>
                        <input type="text" id="mqttTopic" value="hwmonitor/+/metrics">
                    </div>
                </div>
                <div class="row">
                    <div class="form-group">
                        <label>帳號（選填）</label>
                        <input type="text" id="mqttUser">
                    </div>
                    <div class="form-group">
                        <label>密碼（選填）</label>
                        <input type="password" id="mqttPass">
                    </div>
                </div>
            </div>
        </div>

        <!-- 設備設定 -->
        <div id="tab-devices" class="tab-content">
            <div class="card">
                <h2>設備列表</h2>
                <p style="color:#888;font-size:14px;">設備會自動加入，你可以設定別名和顯示時間</p>
                <div id="deviceList" class="device-list">
                    <p style="color:#666;">尚無設備資料</p>
                </div>
            </div>
        </div>

        <!-- 顯示設定 -->
        <div id="tab-display" class="tab-content">
            <div class="card">
                <h2>輪播設定</h2>
                <div class="row">
                    <div class="form-group">
                        <label>預設顯示時間（秒）</label>
                        <input type="number" id="displayTime" value="5" min="1" max="60">
                    </div>
                    <div class="form-group">
                        <label>自動輪播</label>
                        <select id="autoCarousel">
                            <option value="1">開啟</option>
                            <option value="0">關閉</option>
                        </select>
                    </div>
                </div>
            </div>

            <div class="card">
                <h2>警示閾值</h2>
                <div class="threshold-grid">
                    <div class="threshold-item">
                        <label>CPU 警告 %</label>
                        <input type="number" id="cpuWarn" value="70" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>CPU 危險 %</label>
                        <input type="number" id="cpuCrit" value="90" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>RAM 警告 %</label>
                        <input type="number" id="ramWarn" value="70" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>RAM 危險 %</label>
                        <input type="number" id="ramCrit" value="90" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>溫度警告 °C</label>
                        <input type="number" id="tempWarn" value="60" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>溫度危險 °C</label>
                        <input type="number" id="tempCrit" value="80" min="0" max="100">
                    </div>
                </div>
            </div>
        </div>

        <button onclick="saveConfig()">💾 儲存設定</button>
        <button class="secondary" onclick="loadConfig()">🔄 重新載入</button>
        <div id="status" class="status"></div>
    </div>

    <script>
        function showTab(name) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('tab-' + name).classList.add('active');
        }

        function loadConfig() {
            fetch('/api/config')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('mqttServer').value = data.mqtt?.server || '';
                    document.getElementById('mqttPort').value = data.mqtt?.port || 1883;
                    document.getElementById('mqttTopic').value = data.mqtt?.topic || 'hwmonitor/+/metrics';
                    document.getElementById('mqttUser').value = data.mqtt?.user || '';
                    document.getElementById('mqttPass').value = '';

                    document.getElementById('displayTime').value = data.displayTime || 5;
                    document.getElementById('autoCarousel').value = data.autoCarousel ? '1' : '0';

                    if (data.thresholds) {
                        document.getElementById('cpuWarn').value = data.thresholds.cpuWarn || 70;
                        document.getElementById('cpuCrit').value = data.thresholds.cpuCrit || 90;
                        document.getElementById('ramWarn').value = data.thresholds.ramWarn || 70;
                        document.getElementById('ramCrit').value = data.thresholds.ramCrit || 90;
                        document.getElementById('tempWarn').value = data.thresholds.tempWarn || 60;
                        document.getElementById('tempCrit').value = data.thresholds.tempCrit || 80;
                    }

                    renderDevices(data.devices || []);
                });
        }

        function renderDevices(devices) {
            const list = document.getElementById('deviceList');
            if (!devices.length) {
                list.innerHTML = '<p style="color:#666;">尚無設備資料</p>';
                return;
            }

            list.innerHTML = devices.map((d, i) => `
                <div class="device-item">
                    <input type="checkbox" ${d.enabled ? 'checked' : ''}
                           onchange="updateDevice(${i}, 'enabled', this.checked)">
                    <span class="name">${d.hostname}</span>
                    <input type="text" class="alias" value="${d.alias}" maxlength="7"
                           onchange="updateDevice(${i}, 'alias', this.value)" placeholder="別名">
                    <input type="number" class="time" value="${d.time}" min="1" max="60"
                           onchange="updateDevice(${i}, 'time', this.value)">
                    <span>秒</span>
                </div>
            `).join('');
        }

        let deviceData = [];

        function updateDevice(index, field, value) {
            if (!deviceData[index]) return;
            if (field === 'time') value = parseInt(value);
            if (field === 'enabled') value = !!value;
            deviceData[index][field] = value;
        }

        function saveConfig() {
            const config = {
                mqtt: {
                    server: document.getElementById('mqttServer').value,
                    port: parseInt(document.getElementById('mqttPort').value),
                    topic: document.getElementById('mqttTopic').value,
                    user: document.getElementById('mqttUser').value,
                    pass: document.getElementById('mqttPass').value
                },
                displayTime: parseInt(document.getElementById('displayTime').value),
                autoCarousel: document.getElementById('autoCarousel').value === '1',
                thresholds: {
                    cpuWarn: parseInt(document.getElementById('cpuWarn').value),
                    cpuCrit: parseInt(document.getElementById('cpuCrit').value),
                    ramWarn: parseInt(document.getElementById('ramWarn').value),
                    ramCrit: parseInt(document.getElementById('ramCrit').value),
                    tempWarn: parseInt(document.getElementById('tempWarn').value),
                    tempCrit: parseInt(document.getElementById('tempCrit').value)
                },
                devices: deviceData
            };

            fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(config)
            })
            .then(r => r.json())
            .then(data => {
                const status = document.getElementById('status');
                if (data.success) {
                    status.className = 'status success';
                    status.textContent = '✅ 設定已儲存！ESP 將在 3 秒後重啟...';
                    setTimeout(() => location.reload(), 5000);
                } else {
                    status.className = 'status error';
                    status.textContent = '❌ 儲存失敗: ' + (data.message || '未知錯誤');
                }
            })
            .catch(err => {
                document.getElementById('status').className = 'status error';
                document.getElementById('status').textContent = '❌ 連線錯誤';
            });
        }

        // 初始載入
        fetch('/api/config')
            .then(r => r.json())
            .then(data => {
                deviceData = data.devices || [];
                loadConfig();
            });
    </script>
</body>
</html>
)rawliteral";

#endif
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/include/html_monitor.h
git commit -m "feat: add monitor settings WebUI with MQTT/device/threshold config"
```

---

## Task 7: 更新 Web Server

**Files:**
- Modify: `src/include/web_server.h`

**Step 1: 加入監控 API 端點**

修改 `web_server.h`，加入 `/monitor`、`/api/config`、`/api/status` 端點：

```cpp
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
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

    void begin() {
        // 首頁 - WiFi 設定頁面
        _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send_P(200, "text/html", HTML_PAGE);
        });

        // 監控設定頁面
        _server.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send_P(200, "text/html", HTML_MONITOR);
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

            Serial.printf("收到設定: SSID=%s\n", ssid.c_str());

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

                // 延遲後重啟
                delay(3000);
                ESP.restart();
            } else {
                response = "{\"success\":false,\"message\":\"連線失敗\"}";
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
                if (data.containsKey("mqtt")) {
                    JsonObject mqtt = data["mqtt"];
                    strlcpy(cfg.mqttServer, mqtt["server"] | "", sizeof(cfg.mqttServer));
                    cfg.mqttPort = mqtt["port"] | 1883;
                    strlcpy(cfg.mqttTopic, mqtt["topic"] | "hwmonitor/+/metrics", sizeof(cfg.mqttTopic));
                    strlcpy(cfg.mqttUser, mqtt["user"] | "", sizeof(cfg.mqttUser));
                    if (mqtt.containsKey("pass") && strlen(mqtt["pass"]) > 0) {
                        strlcpy(cfg.mqttPass, mqtt["pass"], sizeof(cfg.mqttPass));
                    }
                }

                // 設備
                if (data.containsKey("devices")) {
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
                if (data.containsKey("thresholds")) {
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

                    // 延遲後重啟以套用新設定
                    delay(3000);
                    ESP.restart();
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
        Serial.println("Web Server 已啟動");
    }

private:
    AsyncWebServer _server;
    WiFiManager& _wifiMgr;
    MonitorConfigManager* _monitorConfig = nullptr;
    MQTTClient* _mqtt = nullptr;
};

#endif
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/include/web_server.h
git commit -m "feat: add monitor API endpoints to web server"
```

---

## Task 8: 更新主程式

**Files:**
- Modify: `src/main.cpp`

**Step 1: 整合所有模組**

```cpp
#include <Arduino.h>
#include "include/tft_driver.h"
#include "include/qr_display.h"
#include "include/wifi_manager.h"
#include "include/web_server.h"
#include "include/monitor_config.h"
#include "include/mqtt_client.h"
#include "include/monitor_display.h"

TFTDriver tft;
QRDisplay qr(tft);
WiFiManager wifiMgr;
WebServerManager* webServer = nullptr;
MonitorConfigManager monitorConfig;
MQTTClient mqttClient;
MonitorDisplay* monitorDisplay = nullptr;

enum AppMode {
    MODE_AP_SETUP,      // AP 模式設定 WiFi
    MODE_MONITOR        // 監控模式
};

AppMode currentMode = MODE_AP_SETUP;

// 顯示 AP 模式畫面
void showAPScreen() {
    tft.fillScreen(COLOR_BLACK);

    // 標題
    tft.drawStringCentered(10, "WiFi Setup", COLOR_CYAN, COLOR_BLACK, 2);

    // AP SSID
    String apSSID = wifiMgr.getAPSSID();
    tft.drawStringCentered(45, apSSID.c_str(), COLOR_WHITE, COLOR_BLACK, 1);

    // QR Code (WiFi 連線)
    qr.drawWiFiQR(apSSID.c_str(), nullptr, 10);

    // IP 位址
    tft.drawStringCentered(210, wifiMgr.localIP.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
}

// 顯示已連線畫面（過渡畫面）
void showConnectedScreen() {
    tft.fillScreen(COLOR_BLACK);

    // 標題
    tft.drawStringCentered(10, "Connected", COLOR_GREEN, COLOR_BLACK, 2);

    // SSID
    tft.drawStringCentered(45, wifiMgr.ssid.c_str(), COLOR_WHITE, COLOR_BLACK, 1);

    // QR Code (WebUI URL)
    String url = "http://" + wifiMgr.localIP + "/monitor";
    qr.drawURLQR(url.c_str(), 10);

    // IP 位址
    tft.drawStringCentered(210, wifiMgr.localIP.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
}

// 顯示連線中畫面
void showConnectingScreen() {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(100, "Connecting", COLOR_CYAN, COLOR_BLACK, 2);
    tft.drawStringCentered(130, wifiMgr.ssid.c_str(), COLOR_WHITE, COLOR_BLACK, 1);
}

// 顯示 MQTT 連線中畫面
void showMQTTConnectingScreen() {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(80, "MQTT", COLOR_CYAN, COLOR_BLACK, 2);
    tft.drawStringCentered(110, "Connecting...", COLOR_WHITE, COLOR_BLACK, 1);
    tft.drawStringCentered(150, monitorConfig.config.mqttServer, COLOR_GRAY, COLOR_BLACK, 1);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP12 System Monitor ===");

    // 初始化 TFT
    tft.begin();
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(110, "Starting...", COLOR_WHITE, COLOR_BLACK, 2);

    // 初始化 WiFi Manager
    wifiMgr.begin();

    // 初始化監控設定
    monitorConfig.begin();
    monitorConfig.load();

    // 嘗試載入並連線 WiFi
    if (wifiMgr.loadConfig()) {
        showConnectingScreen();

        if (wifiMgr.connectWiFi()) {
            // WiFi 連線成功
            showConnectedScreen();
            delay(2000);  // 顯示連線資訊 2 秒

            // 進入監控模式
            currentMode = MODE_MONITOR;

            // 初始化 MQTT
            mqttClient.begin(monitorConfig);

            // 如果有設定 MQTT 伺服器，則連線
            if (strlen(monitorConfig.config.mqttServer) > 0) {
                showMQTTConnectingScreen();
                mqttClient.connect();
                delay(1000);
            }

            // 初始化監控顯示
            monitorDisplay = new MonitorDisplay(tft, mqttClient, monitorConfig);
            monitorDisplay->begin();

            // 啟動 Web Server
            webServer = new WebServerManager(wifiMgr);
            webServer->setMonitorConfig(&monitorConfig);
            webServer->setMQTTClient(&mqttClient);
            webServer->begin();

            Serial.println("監控模式已啟動");
            Serial.printf("WebUI: http://%s/monitor\n", wifiMgr.localIP.c_str());
            return;
        }
    }

    // 無設定或連線失敗 - 進入 AP 模式
    currentMode = MODE_AP_SETUP;
    Serial.println("進入 AP 模式");
    wifiMgr.startAP();
    showAPScreen();

    // 開始背景掃描 WiFi
    wifiMgr.startScan();

    // 啟動 Web Server
    webServer = new WebServerManager(wifiMgr);
    webServer->setMonitorConfig(&monitorConfig);
    webServer->begin();
}

void loop() {
    if (currentMode == MODE_MONITOR) {
        // 監控模式
        mqttClient.loop();
        if (monitorDisplay) {
            monitorDisplay->loop();
        }
    }

    delay(10);
}
```

**Step 2: 驗證編譯**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run`
Expected: BUILD SUCCESS

**Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate MQTT client and monitor display in main program"
```

---

## Task 9: 測試與除錯

**Step 1: 編譯並上傳**

Run: `cd /home/jwj/文件/PlatformIO/Projects/ESP12-Blink && pio run -t upload`
Expected: Upload successful

**Step 2: 測試流程**

1. 首次啟動 → AP 模式 → 設定 WiFi
2. 連線成功後進入監控模式
3. 瀏覽 `http://<IP>/monitor` 設定 MQTT
4. 重啟後自動連接 MQTT 並顯示數據

**Step 3: Commit**

```bash
git add -A
git commit -m "feat: complete ESP12 MQTT system monitor implementation"
```

---

## 總結

完成後的功能：

| 功能 | 說明 |
|------|------|
| MQTT 訂閱 | 自動訂閱 `hwmonitor/+/metrics` |
| 多設備輪播 | 可設定每台顯示時間 |
| 設備別名 | 預設取 hostname 前 4 字 |
| 變色警示 | 綠→黃→紅 依閾值變色 |
| 離線警示 | 設備離線時閃爍提示 |
| WebUI 設定 | MQTT/設備/閾值 完整設定 |
| 自動儲存 | 設定存於 LittleFS |

WebUI 路徑：
- WiFi 設定：`http://<IP>/`
- 監控設定：`http://<IP>/monitor`
