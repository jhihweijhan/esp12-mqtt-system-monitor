#ifndef MONITOR_DISPLAY_H
#define MONITOR_DISPLAY_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
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
        _pendingVisibleUpdate = true;
    }

    void notifyMetricsUpdated(const char* hostname) {
        if (!hostname || hostname[0] == '\0') {
            _pendingVisibleUpdate = true;
            return;
        }
        DeviceMetrics* current = _mqtt.getOnlineDevice(_currentDevice);
        if (!current || strcmp(current->hostname, hostname) == 0) {
            _pendingVisibleUpdate = true;
        }
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

        // 事件驅動快刷 + 閒置慢刷
        uint16_t refreshInterval = computeDisplayRefreshIntervalMs(_pendingVisibleUpdate, _forceRedraw);
        if (now - _lastRefresh > refreshInterval) {
            _lastRefresh = now;
            refresh();
            _pendingVisibleUpdate = false;
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
        uint8_t onlineCount = _mqtt.getOnlineCount();

        // 有在線設備時，永遠優先顯示在線設備，不應落入 OFFLINE 畫面
        if (onlineCount > 0) {
            // 線上數量變動後，校正索引避免越界誤判
            if (_currentDevice >= onlineCount) {
                Serial.printf("Adjust current device index: %u -> 0 (online=%u)\n",
                              _currentDevice, onlineCount);
                _currentDevice = 0;
                _forceRedraw = true;
            }

            DeviceMetrics* dev = _mqtt.getOnlineDevice(_currentDevice);
            if (!dev) {
                // 防禦性回退：理論上不應發生，但若發生仍優先嘗試第一台在線設備
                _currentDevice = 0;
                dev = _mqtt.getOnlineDevice(0);
            }

            if (dev) {
                showDevice(dev);
                return;
            }
        }

        // 僅在沒有在線設備時，才檢查離線警示
        const char* offlineDevice = nullptr;
        for (uint8_t i = 0; i < _mqtt.deviceCount; i++) {
            DeviceConfig* cfg = _config.getOrCreateDevice(_mqtt.devices[i].hostname);
            if (cfg && cfg->enabled && !_mqtt.devices[i].online) {
                offlineDevice = _mqtt.devices[i].hostname;
                break;
            }
        }

        if (offlineDevice) {
            showOfflineDevice(offlineDevice);
        } else {
            showNoDevice();
        }
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
    bool _pendingVisibleUpdate = false;
    char _lastHostname[32] = "";

    // 快取上次繪製的值，避免不必要的重繪（size 1 逐像素繪製很慢）
    int _lastGpuHspTemp = -999;
    int _lastGpuMemTemp = -999;

    void drawLocalIpCentered(int16_t y, uint16_t color) {
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        _tft.drawStringCentered(y, ipBuf, color, COLOR_BLACK, 1);
    }

    void showDevice(DeviceMetrics* dev) {
        // 取得設備別名
        DeviceConfig* cfg = _config.getOrCreateDevice(dev->hostname);
        // 使用別名，若為空則使用 hostname
        const char* alias = (cfg && strlen(cfg->alias) > 0) ? cfg->alias : dev->hostname;

        // 檢查是否需要完整重繪（設備切換或強制重繪）
        bool needHeaderRedraw = _forceRedraw || strcmp(_lastHostname, dev->hostname) != 0;
        if (needHeaderRedraw) {
            _tft.fillScreen(COLOR_BLACK);
            strcpy(_lastHostname, dev->hostname);
            _forceRedraw = false;
            // 重置快取值，強制全部重繪
            _lastGpuHspTemp = -999;
            _lastGpuMemTemp = -999;

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
        _tft.drawStringPadded(64, y, buf, cpuColor, COLOR_BLACK, 2, 80);

        int cpuTemp = (int)dev->cpuTemp;
        snprintf(buf, sizeof(buf), "%2dC", cpuTemp);
        uint16_t tempColor = (cpuTemp >= th.tempCrit) ? COLOR_RED :
                             (cpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
        _tft.drawStringPadded(152, y, buf, tempColor, COLOR_BLACK, 2, 80);

        y += 36;

        // RAM（大字）
        _tft.drawString(8, y, "RAM", COLOR_WHITE, COLOR_BLACK, 2);

        int ramPct = (int)dev->ramPercent;
        snprintf(buf, sizeof(buf), "%3d%%", ramPct);
        uint16_t ramColor = (ramPct >= th.ramCrit) ? COLOR_RED :
                            (ramPct >= th.ramWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.drawStringPadded(64, y, buf, ramColor, COLOR_BLACK, 2, 70);

        // RAM 用量
        snprintf(buf, sizeof(buf), "%.1f/%.0fG", dev->ramUsedGB, dev->ramTotalGB);
        _tft.drawStringPadded(136, y, buf, COLOR_GRAY, COLOR_BLACK, 1, 100);

        y += 36;
        yield();

        // GPU（如果有）
        if (dev->gpuPercent > 0 || dev->gpuTemp > 0) {
            _tft.drawString(8, y, "GPU", COLOR_WHITE, COLOR_BLACK, 2);

            int gpuPct = (int)dev->gpuPercent;
            snprintf(buf, sizeof(buf), "%3d%%", gpuPct);
            uint16_t gpuColor = (gpuPct >= th.gpuCrit) ? COLOR_RED :
                                (gpuPct >= th.gpuWarn) ? COLOR_YELLOW : COLOR_GREEN;
            _tft.drawStringPadded(64, y, buf, gpuColor, COLOR_BLACK, 2, 80);

            int gpuTemp = (int)dev->gpuTemp;
            snprintf(buf, sizeof(buf), "%2dC", gpuTemp);
            uint16_t gTempColor = (gpuTemp >= th.tempCrit) ? COLOR_RED :
                                  (gpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
            _tft.drawStringPadded(152, y, buf, gTempColor, COLOR_BLACK, 2, 80);

            y += 32;  // size 2 字高 32px，不能用 24 否則會蓋到下一行

            // 熱點 & VRAM 溫度（快取值，僅在變化時重繪）
            if (dev->gpuHotspotTemp > 0 || dev->gpuMemTemp > 0) {
                int hspTemp = (int)dev->gpuHotspotTemp;
                if (hspTemp != _lastGpuHspTemp) {
                    _lastGpuHspTemp = hspTemp;
                    if (hspTemp > 0) {
                        snprintf(buf, sizeof(buf), "HSP:%dC", hspTemp);
                        uint16_t hspColor = (hspTemp >= th.tempCrit) ? COLOR_RED :
                                            (hspTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
                        _tft.drawStringPadded(8, y, buf, hspColor, COLOR_BLACK, 1, 72);
                    } else {
                        _tft.fillRect(8, y, 72, FONT_HEIGHT, COLOR_BLACK);
                    }
                }
                int mTemp = (int)dev->gpuMemTemp;
                if (mTemp != _lastGpuMemTemp) {
                    _lastGpuMemTemp = mTemp;
                    if (mTemp > 0) {
                        snprintf(buf, sizeof(buf), "MEM:%dC", mTemp);
                        uint16_t mColor = (mTemp >= th.tempCrit) ? COLOR_RED :
                                          (mTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
                        _tft.drawStringPadded(88, y, buf, mColor, COLOR_BLACK, 1, 72);
                    } else {
                        _tft.fillRect(88, y, 72, FONT_HEIGHT, COLOR_BLACK);
                    }
                }
                y += 16;
            }

            // GPU 記憶體
            snprintf(buf, sizeof(buf), "VRAM: %d%%", (int)dev->gpuMemPercent);
            _tft.drawStringPadded(8, y, buf, COLOR_GRAY, COLOR_BLACK, 1, 120);
            y += 16;
        }

        yield();

        // 網路
        _tft.drawString(8, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "v%.1fM", dev->netRxMbps);
        _tft.drawStringPadded(40, y, buf, COLOR_GREEN, COLOR_BLACK, 1, 70);
        snprintf(buf, sizeof(buf), "^%.1fM", dev->netTxMbps);
        _tft.drawStringPadded(112, y, buf, COLOR_CYAN, COLOR_BLACK, 1, 70);

        y += 16;

        // 磁碟
        _tft.drawString(8, y, "DISK", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "R:%.1fM", dev->diskReadMBs);
        _tft.drawStringPadded(48, y, buf, COLOR_WHITE, COLOR_BLACK, 1, 78);
        snprintf(buf, sizeof(buf), "W:%.1fM", dev->diskWriteMBs);
        _tft.drawStringPadded(128, y, buf, COLOR_WHITE, COLOR_BLACK, 1, 78);

        yield();

        // 底部狀態列
        // IP 位址
        y = 204;
        drawLocalIpCentered(y, COLOR_YELLOW);

        // MQTT 狀態 + 更新時間
        y = 222;
        if (_mqtt.isConnectedForDisplay()) {
            _tft.drawString(8, y, "MQTT OK", COLOR_GREEN, COLOR_BLACK, 1);
        } else {
            _tft.drawString(8, y, "MQTT --", COLOR_RED, COLOR_BLACK, 1);
        }

        unsigned long age = (millis() - dev->lastUpdate) / 1000;
        snprintf(buf, sizeof(buf), "%lus ago", age);
        _tft.drawStringPadded(168, y, buf, COLOR_GRAY, COLOR_BLACK, 1, 70);
    }

    void showNoDevice() {
        if (!_forceRedraw) return;
        _forceRedraw = false;

        _tft.fillScreen(COLOR_BLACK);
        _ui.drawDeviceHeader("Monitor", true);

        _tft.drawStringCentered(100, "Waiting", COLOR_CYAN, COLOR_BLACK, 2);
        _tft.drawStringCentered(130, "for data...", COLOR_GRAY, COLOR_BLACK, 1);

        if (!_mqtt.isConnectedForDisplay()) {
            _tft.drawStringCentered(160, "MQTT not connected", COLOR_RED, COLOR_BLACK, 1);
        }

        // 底部顯示 IP
        drawLocalIpCentered(204, COLOR_YELLOW);
    }

    void showOfflineDevice(const char* hostname) {
        DeviceConfig* cfg = _config.getOrCreateDevice(hostname);
        const char* alias = (cfg && strlen(cfg->alias) > 0) ? cfg->alias : hostname;

        bool needRedraw = _forceRedraw || strcmp(_lastHostname, hostname) != 0;
        if (!needRedraw) return;

        _tft.fillScreen(COLOR_BLACK);
        strlcpy(_lastHostname, hostname, sizeof(_lastHostname));
        _forceRedraw = false;
        _ui.drawDeviceHeader(alias, false);

        int16_t y = 36;
        _tft.drawString(8, y, "CPU", COLOR_WHITE, COLOR_BLACK, 2);
        _tft.drawStringPadded(64, y, "--%", COLOR_GRAY, COLOR_BLACK, 2, 80);
        _tft.drawStringPadded(152, y, "--C", COLOR_GRAY, COLOR_BLACK, 2, 80);

        y += 36;
        _tft.drawString(8, y, "RAM", COLOR_WHITE, COLOR_BLACK, 2);
        _tft.drawStringPadded(64, y, "--%", COLOR_GRAY, COLOR_BLACK, 2, 70);
        _tft.drawStringPadded(136, y, "--/--G", COLOR_GRAY, COLOR_BLACK, 1, 100);

        y += 36;
        _tft.drawString(8, y, "GPU", COLOR_WHITE, COLOR_BLACK, 2);
        _tft.drawStringPadded(64, y, "--%", COLOR_GRAY, COLOR_BLACK, 2, 80);
        _tft.drawStringPadded(152, y, "--C", COLOR_GRAY, COLOR_BLACK, 2, 80);

        y += 36;
        _tft.drawString(8, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        _tft.drawStringPadded(40, y, "v--", COLOR_GRAY, COLOR_BLACK, 1, 70);
        _tft.drawStringPadded(112, y, "^--", COLOR_GRAY, COLOR_BLACK, 1, 70);

        y += 16;
        _tft.drawString(8, y, "DISK", COLOR_GRAY, COLOR_BLACK, 1);
        _tft.drawStringPadded(48, y, "R:--", COLOR_GRAY, COLOR_BLACK, 1, 78);
        _tft.drawStringPadded(128, y, "W:--", COLOR_GRAY, COLOR_BLACK, 1, 78);

        y = 204;
        drawLocalIpCentered(y, COLOR_YELLOW);

        y = 222;
        if (_mqtt.isConnectedForDisplay()) {
            _tft.drawString(8, y, "MQTT OK", COLOR_GREEN, COLOR_BLACK, 1);
        } else {
            _tft.drawString(8, y, "MQTT --", COLOR_RED, COLOR_BLACK, 1);
        }
        _tft.drawStringPadded(168, y, "OFFLINE", COLOR_RED, COLOR_BLACK, 1, 70);
    }
};

#endif
