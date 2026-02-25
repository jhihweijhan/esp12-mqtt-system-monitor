#ifndef MONITOR_DISPLAY_H
#define MONITOR_DISPLAY_H

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "connection_policy.h"
#include "device_store.h"
#include "monitor_config.h"
#include "mqtt_transport.h"
#include "tft_driver.h"
#include "ui_components.h"

class MonitorDisplay {
public:
    MonitorDisplay(TFTDriver& tft,
                   DeviceStore& store,
                   MQTTTransport& mqtt,
                   MonitorConfigManager& config)
        : _tft(tft), _ui(tft), _store(store), _mqtt(mqtt), _config(config) {}

    void begin() {
        _currentDevice = 0;
        _lastSwitch = millis();
        _lastRefresh = 0;
        _lastFooterUpdate = 0;
        _pendingVisibleUpdate = true;
        _forceRedraw = true;
        _lastHostname[0] = '\0';
    }

    void notifyMetricsUpdated(const char* hostname) {
        if (!hostname || hostname[0] == '\0') {
            _pendingVisibleUpdate = true;
            return;
        }

        DeviceSlot* current = _store.getOnlineByIndex(_currentDevice, &_config);
        if (!current || strcmp(current->hostname, hostname) == 0) {
            _pendingVisibleUpdate = true;
        }
    }

    void loop() {
        const unsigned long now = millis();
        autoRotateIfNeeded(now);

        const uint16_t refreshInterval = computeDisplayRefreshIntervalMs(_pendingVisibleUpdate, _forceRedraw);
        if (now - _lastRefresh >= refreshInterval) {
            _lastRefresh = now;
            refresh(now);
            _pendingVisibleUpdate = false;
        }
    }

private:
    TFTDriver& _tft;
    UIComponents _ui;
    DeviceStore& _store;
    MQTTTransport& _mqtt;
    MonitorConfigManager& _config;

    uint8_t _currentDevice = 0;
    unsigned long _lastSwitch = 0;
    unsigned long _lastRefresh = 0;
    unsigned long _lastFooterUpdate = 0;
    bool _forceRedraw = true;
    bool _pendingVisibleUpdate = false;
    char _lastHostname[32] = "";

    void autoRotateIfNeeded(unsigned long now) {
        uint8_t onlineCount = _store.getOnlineCount(&_config);
        if (!_config.config.autoCarousel || onlineCount <= 1) {
            return;
        }

        DeviceSlot* slot = _store.getOnlineByIndex(_currentDevice, &_config);
        uint16_t displayTime = _config.config.defaultDisplayTime;
        if (slot) {
            DeviceConfig* devCfg = _config.getOrCreateDevice(slot->hostname);
            if (devCfg) {
                displayTime = devCfg->displayTime;
            }
        }

        if (now - _lastSwitch > (unsigned long)displayTime * 1000UL) {
            _currentDevice = (_currentDevice + 1) % onlineCount;
            _lastSwitch = now;
            _forceRedraw = true;
            _pendingVisibleUpdate = true;
        }
    }

    void refresh(unsigned long now) {
        uint8_t onlineCount = _store.getOnlineCount(&_config);
        if (onlineCount > 0) {
            if (_currentDevice >= onlineCount) {
                _currentDevice = 0;
                _forceRedraw = true;
            }

            DeviceSlot* slot = _store.getOnlineByIndex(_currentDevice, &_config);
            if (slot) {
                showDevice(slot, onlineCount, now);
                return;
            }
        }

        const char* offlineDevice = findFirstOfflineEnabledDevice();
        if (offlineDevice) {
            showOfflineDevice(offlineDevice);
        } else {
            showNoDevice();
        }
    }

    const char* findFirstOfflineEnabledDevice() {
        for (uint8_t i = 0; i < _store.deviceCount; i++) {
            DeviceSlot* slot = _store.getByIndex(i);
            if (!slot) {
                continue;
            }

            DeviceConfig* cfg = _config.getOrCreateDevice(slot->hostname);
            if (!cfg || !cfg->enabled) {
                continue;
            }

            if (!slot->online) {
                return slot->hostname;
            }
        }
        return nullptr;
    }

    void showDevice(DeviceSlot* slot, uint8_t onlineCount, unsigned long now) {
        DeviceConfig* cfg = _config.getOrCreateDevice(slot->hostname);
        const char* alias = (cfg && strlen(cfg->alias) > 0) ? cfg->alias : slot->hostname;

        uint16_t dirty = _store.consumeDirtyMask(slot);
        bool headerRedraw = shouldRedrawDeviceHeader(
            _forceRedraw,
            strcmp(_lastHostname, slot->hostname) != 0,
            dirty);
        if (headerRedraw) {
            dirty = DIRTY_ALL;
            _tft.fillScreen(COLOR_BLACK);
            _ui.drawDeviceHeader(alias, true);

            if (onlineCount > 1) {
                char indicator[16];
                snprintf(indicator, sizeof(indicator), "%d/%d", _currentDevice + 1, onlineCount);
                _tft.drawString(200, 8, indicator, COLOR_GRAY, 0x1082, 1);
            }

            strlcpy(_lastHostname, slot->hostname, sizeof(_lastHostname));
            _forceRedraw = false;
        }

        const MetricsFrameV2& frame = slot->frame;
        ThresholdConfig& th = _config.config.thresholds;

        if (dirty & DIRTY_CPU) {
            drawCpuRow(frame, th);
        }
        if (dirty & DIRTY_RAM) {
            drawRamRow(frame, th);
        }
        if (dirty & DIRTY_GPU) {
            drawGpuRows(frame, th);
        }
        if (dirty & DIRTY_NET) {
            drawNetRow(frame);
        }
        if (dirty & DIRTY_DISK) {
            drawDiskRow(frame);
        }

        if (headerRedraw || dirty != DIRTY_NONE || now - _lastFooterUpdate >= 1000UL) {
            drawFooter(frame, now, slot->lastUpdateMs);
            _lastFooterUpdate = now;
        }
    }

    void drawCpuRow(const MetricsFrameV2& frame, ThresholdConfig& th) {
        int y = 36;
        char buf[20];
        int cpuPct = roundedPercent(frame.cpuPctX10);
        int cpuTemp = roundedTempC(frame.cpuTempCX10);

        _tft.drawString(8, y, "CPU", COLOR_WHITE, COLOR_BLACK, 2);
        snprintf(buf, sizeof(buf), "%3d%%", cpuPct);
        uint16_t cpuColor = (cpuPct >= th.cpuCrit) ? COLOR_RED : (cpuPct >= th.cpuWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.drawStringPadded(64, y, buf, cpuColor, COLOR_BLACK, 2, 80);

        snprintf(buf, sizeof(buf), "%2dC", cpuTemp);
        uint16_t tempColor = (cpuTemp >= th.tempCrit) ? COLOR_RED : (cpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
        _tft.drawStringPadded(152, y, buf, tempColor, COLOR_BLACK, 2, 80);
    }

    void drawRamRow(const MetricsFrameV2& frame, ThresholdConfig& th) {
        int y = 72;
        char buf[24];
        int ramPct = roundedPercent(frame.ramPctX10);

        _tft.drawString(8, y, "RAM", COLOR_WHITE, COLOR_BLACK, 2);
        snprintf(buf, sizeof(buf), "%3d%%", ramPct);
        uint16_t ramColor = (ramPct >= th.ramCrit) ? COLOR_RED : (ramPct >= th.ramWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.drawStringPadded(64, y, buf, ramColor, COLOR_BLACK, 2, 70);

        snprintf(buf, sizeof(buf), "%u/%uM", frame.ramUsedMB, frame.ramTotalMB);
        _tft.drawStringPadded(136, y, buf, COLOR_GRAY, COLOR_BLACK, 1, 100);
    }

    void drawGpuRows(const MetricsFrameV2& frame, ThresholdConfig& th) {
        int y = 108;
        char buf[24];

        _tft.drawString(8, y, "GPU", COLOR_WHITE, COLOR_BLACK, 2);

        int gpuPct = roundedPercent(frame.gpuPctX10);
        snprintf(buf, sizeof(buf), "%3d%%", gpuPct);
        uint16_t gpuColor = (gpuPct >= th.gpuCrit) ? COLOR_RED : (gpuPct >= th.gpuWarn) ? COLOR_YELLOW : COLOR_GREEN;
        _tft.drawStringPadded(64, y, buf, gpuColor, COLOR_BLACK, 2, 80);

        int gpuTemp = roundedTempC(frame.gpuTempCX10);
        snprintf(buf, sizeof(buf), "%2dC", gpuTemp);
        uint16_t tempColor = (gpuTemp >= th.tempCrit) ? COLOR_RED : (gpuTemp >= th.tempWarn) ? COLOR_YELLOW : COLOR_CYAN;
        _tft.drawStringPadded(152, y, buf, tempColor, COLOR_BLACK, 2, 80);

        y += 32;
        int hspTemp = roundedTempC(frame.gpuHotspotCX10);
        int memTemp = roundedTempC(frame.gpuMemTempCX10);
        snprintf(buf, sizeof(buf), "HSP:%dC", hspTemp);
        _tft.drawStringPadded(8, y, buf, COLOR_CYAN, COLOR_BLACK, 1, 72);
        snprintf(buf, sizeof(buf), "MEM:%dC", memTemp);
        _tft.drawStringPadded(88, y, buf, COLOR_CYAN, COLOR_BLACK, 1, 72);

        y += 16;
        snprintf(buf, sizeof(buf), "VRAM: %d%%", roundedPercent(frame.gpuMemPctX10));
        _tft.drawStringPadded(8, y, buf, COLOR_GRAY, COLOR_BLACK, 1, 120);
    }

    void drawNetRow(const MetricsFrameV2& frame) {
        int y = 172;
        char buf[20];
        _tft.drawString(8, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "v%.1fM", kbpsToMbps(frame.netRxKbps));
        _tft.drawStringPadded(40, y, buf, COLOR_GREEN, COLOR_BLACK, 1, 70);
        snprintf(buf, sizeof(buf), "^%.1fM", kbpsToMbps(frame.netTxKbps));
        _tft.drawStringPadded(112, y, buf, COLOR_CYAN, COLOR_BLACK, 1, 70);
    }

    void drawDiskRow(const MetricsFrameV2& frame) {
        int y = 188;
        char buf[20];
        _tft.drawString(8, y, "DISK", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "R:%.1fM", kbpsToMBps(frame.diskReadKBps));
        _tft.drawStringPadded(48, y, buf, COLOR_WHITE, COLOR_BLACK, 1, 78);
        snprintf(buf, sizeof(buf), "W:%.1fM", kbpsToMBps(frame.diskWriteKBps));
        _tft.drawStringPadded(128, y, buf, COLOR_WHITE, COLOR_BLACK, 1, 78);
    }

    void drawFooter(const MetricsFrameV2&, unsigned long now, unsigned long lastUpdateMs) {
        char buf[20];

        String ip = WiFi.localIP().toString();
        _tft.drawStringCentered(204, ip.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);

        if (_mqtt.isConnectedForDisplay()) {
            _tft.drawString(8, 222, "MQTT OK", COLOR_GREEN, COLOR_BLACK, 1);
        } else {
            _tft.drawString(8, 222, "MQTT --", COLOR_RED, COLOR_BLACK, 1);
        }

        unsigned long ageSec = (now - lastUpdateMs) / 1000UL;
        snprintf(buf, sizeof(buf), "%lus ago", ageSec);
        _tft.drawStringPadded(168, 222, buf, COLOR_GRAY, COLOR_BLACK, 1, 70);
    }

    void showNoDevice() {
        if (!_forceRedraw) {
            return;
        }

        _forceRedraw = false;
        _lastHostname[0] = '\0';

        _tft.fillScreen(COLOR_BLACK);
        _ui.drawDeviceHeader("Monitor", true);
        _tft.drawStringCentered(100, "Waiting", COLOR_CYAN, COLOR_BLACK, 2);
        _tft.drawStringCentered(130, "for metrics v2", COLOR_GRAY, COLOR_BLACK, 1);

        if (!_mqtt.isConnectedForDisplay()) {
            _tft.drawStringCentered(160, "MQTT not connected", COLOR_RED, COLOR_BLACK, 1);
        }

        String ip = WiFi.localIP().toString();
        _tft.drawStringCentered(204, ip.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
    }

    void showOfflineDevice(const char* hostname) {
        bool needRedraw = _forceRedraw || strcmp(_lastHostname, hostname) != 0;
        if (!needRedraw) {
            return;
        }

        DeviceConfig* cfg = _config.getOrCreateDevice(hostname);
        const char* alias = (cfg && strlen(cfg->alias) > 0) ? cfg->alias : hostname;

        _tft.fillScreen(COLOR_BLACK);
        strlcpy(_lastHostname, hostname, sizeof(_lastHostname));
        _forceRedraw = false;

        _ui.drawDeviceHeader(alias, false);
        _tft.drawStringCentered(96, "OFFLINE", COLOR_RED, COLOR_BLACK, 2);
        _tft.drawStringCentered(128, "No updates", COLOR_GRAY, COLOR_BLACK, 1);

        if (_mqtt.isConnectedForDisplay()) {
            _tft.drawString(8, 222, "MQTT OK", COLOR_GREEN, COLOR_BLACK, 1);
        } else {
            _tft.drawString(8, 222, "MQTT --", COLOR_RED, COLOR_BLACK, 1);
        }

        _tft.drawStringPadded(168, 222, "OFFLINE", COLOR_RED, COLOR_BLACK, 1, 70);
    }
};

#endif
