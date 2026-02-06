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
        // 取得設備別名
        DeviceConfig* cfg = _config.getOrCreateDevice(dev->hostname);
        const char* alias = cfg ? cfg->alias : dev->hostname;

        // 檢查是否需要完整重繪（設備切換或強制重繪）
        bool needHeaderRedraw = _forceRedraw || strcmp(_lastHostname, dev->hostname) != 0;
        if (needHeaderRedraw) {
            _tft.fillScreen(COLOR_BLACK);
            strcpy(_lastHostname, dev->hostname);
            _forceRedraw = false;

            // 標題列（只在需要時繪製）
            _ui.drawDeviceHeader(alias, true);

            // 線上設備數量指示
            uint8_t onlineCount = _mqtt.getOnlineCount();
            if (onlineCount > 1) {
                char indicator[16];
                snprintf(indicator, sizeof(indicator), "%d/%d", _currentDevice + 1, onlineCount);
                _tft.drawString(200, 8, indicator, COLOR_GRAY, 0x1082, 1);
            }
        }

        yield();  // 讓 WDT 不會超時

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
        _tft.fillRect(64, y, 80, 16, COLOR_BLACK);  // 清除數值區域
        _tft.drawString(64, y, buf, cpuColor, COLOR_BLACK, 2);

        int cpuTemp = (int)dev->cpuTemp;
        snprintf(buf, sizeof(buf), "%2dC", cpuTemp);
        uint16_t tempColor = (cpuTemp >= th.tempCrit) ? COLOR_RED :
                             (cpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
        _tft.fillRect(152, y, 80, 16, COLOR_BLACK);  // 清除數值區域
        _tft.drawString(152, y, buf, tempColor, COLOR_BLACK, 2);

        y += 36;

        // RAM（大字）
        _tft.drawString(8, y, "RAM", COLOR_WHITE, COLOR_BLACK, 2);

        int ramPct = (int)dev->ramPercent;
        snprintf(buf, sizeof(buf), "%3d%%", ramPct);
        uint16_t ramColor = (ramPct >= th.ramCrit) ? COLOR_RED :
                            (ramPct >= th.ramWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.fillRect(64, y, 70, 16, COLOR_BLACK);  // 清除數值區域
        _tft.drawString(64, y, buf, ramColor, COLOR_BLACK, 2);

        // RAM 用量
        snprintf(buf, sizeof(buf), "%.1f/%.0fG", dev->ramUsedGB, dev->ramTotalGB);
        _tft.fillRect(136, y, 100, 16, COLOR_BLACK);  // 清除數值區域
        _tft.drawString(136, y, buf, COLOR_GRAY, COLOR_BLACK, 1);

        y += 36;

        yield();  // 讓 WDT 不會超時

        // GPU（如果有）
        if (dev->gpuPercent > 0 || dev->gpuTemp > 0) {
            _tft.drawString(8, y, "GPU", COLOR_WHITE, COLOR_BLACK, 2);

            int gpuPct = (int)dev->gpuPercent;
            snprintf(buf, sizeof(buf), "%3d%%", gpuPct);
            uint16_t gpuColor = (gpuPct >= th.gpuCrit) ? COLOR_RED :
                                (gpuPct >= th.gpuWarn) ? COLOR_YELLOW : COLOR_GREEN;
            _tft.fillRect(64, y, 80, 16, COLOR_BLACK);  // 清除數值區域
            _tft.drawString(64, y, buf, gpuColor, COLOR_BLACK, 2);

            int gpuTemp = (int)dev->gpuTemp;
            snprintf(buf, sizeof(buf), "%2dC", gpuTemp);
            uint16_t gTempColor = (gpuTemp >= th.tempCrit) ? COLOR_RED :
                                  (gpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
            _tft.fillRect(152, y, 80, 16, COLOR_BLACK);  // 清除數值區域
            _tft.drawString(152, y, buf, gTempColor, COLOR_BLACK, 2);

            y += 24;

            // GPU 記憶體
            snprintf(buf, sizeof(buf), "VRAM: %d%%", (int)dev->gpuMemPercent);
            _tft.fillRect(8, y, 120, 16, COLOR_BLACK);  // 清除數值區域
            _tft.drawString(8, y, buf, COLOR_GRAY, COLOR_BLACK, 1);
            y += 20;
        }

        // 網路
        _tft.drawString(8, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        _tft.fillRect(40, y, 70, 16, COLOR_BLACK);  // 清除數值區域
        snprintf(buf, sizeof(buf), "v%.1fM", dev->netRxMbps);
        _tft.drawString(40, y, buf, COLOR_GREEN, COLOR_BLACK, 1);
        _tft.fillRect(112, y, 70, 16, COLOR_BLACK);  // 清除數值區域
        snprintf(buf, sizeof(buf), "^%.1fM", dev->netTxMbps);
        _tft.drawString(112, y, buf, COLOR_CYAN, COLOR_BLACK, 1);

        y += 20;

        // 磁碟
        _tft.drawString(8, y, "DISK", COLOR_GRAY, COLOR_BLACK, 1);
        _tft.fillRect(48, y, 78, 16, COLOR_BLACK);  // 清除數值區域
        snprintf(buf, sizeof(buf), "R:%.1fM", dev->diskReadMBs);
        _tft.drawString(48, y, buf, COLOR_WHITE, COLOR_BLACK, 1);
        _tft.fillRect(128, y, 78, 16, COLOR_BLACK);  // 清除數值區域
        snprintf(buf, sizeof(buf), "W:%.1fM", dev->diskWriteMBs);
        _tft.drawString(128, y, buf, COLOR_WHITE, COLOR_BLACK, 1);

        // 底部狀態列
        // IP 位址
        y = 204;
        String ip = WiFi.localIP().toString();
        _tft.drawStringCentered(y, ip.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);

        // MQTT 狀態 + 更新時間
        y = 222;
        if (_mqtt.isConnected()) {
            _tft.drawString(8, y, "MQTT OK", COLOR_GREEN, COLOR_BLACK, 1);
        } else {
            _tft.drawString(8, y, "MQTT --", COLOR_RED, COLOR_BLACK, 1);
        }

        unsigned long age = (millis() - dev->lastUpdate) / 1000;
        snprintf(buf, sizeof(buf), "%lus ago", age);
        _tft.fillRect(168, y, 70, 16, COLOR_BLACK);  // 清除數值區域
        _tft.drawString(168, y, buf, COLOR_GRAY, COLOR_BLACK, 1);
    }

    void showNoDevice() {
        if (!_forceRedraw) return;
        _forceRedraw = false;

        _tft.fillScreen(COLOR_BLACK);
        _ui.drawDeviceHeader("Monitor", true);

        _tft.drawStringCentered(100, "Waiting", COLOR_CYAN, COLOR_BLACK, 2);
        _tft.drawStringCentered(130, "for data...", COLOR_GRAY, COLOR_BLACK, 1);

        if (!_mqtt.isConnected()) {
            _tft.drawStringCentered(160, "MQTT not connected", COLOR_RED, COLOR_BLACK, 1);
        }

        // 底部顯示 IP
        String ip = WiFi.localIP().toString();
        _tft.drawStringCentered(204, ip.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
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
