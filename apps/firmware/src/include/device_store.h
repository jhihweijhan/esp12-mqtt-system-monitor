#ifndef DEVICE_STORE_H
#define DEVICE_STORE_H

#include <Arduino.h>
#include <string.h>

#include "metrics_v2.h"
#include "monitor_config.h"

struct DeviceSlot {
    char hostname[32];
    bool inUse;
    bool online;
    unsigned long lastUpdateMs;
    MetricsFrameV2 frame;
    uint16_t dirtyMask;
};

class DeviceStore {
public:
    DeviceSlot devices[MAX_DEVICES];
    uint8_t deviceCount = 0;

    void begin() {
        deviceCount = 0;
        for (uint8_t i = 0; i < MAX_DEVICES; i++) {
            devices[i].hostname[0] = '\0';
            devices[i].inUse = false;
            devices[i].online = false;
            devices[i].lastUpdateMs = 0;
            devices[i].dirtyMask = DIRTY_NONE;
        }
    }

    DeviceSlot* getByHostname(const char* hostname) {
        if (!hostname) {
            return nullptr;
        }
        for (uint8_t i = 0; i < MAX_DEVICES; i++) {
            if (devices[i].inUse && strcmp(devices[i].hostname, hostname) == 0) {
                return &devices[i];
            }
        }
        return nullptr;
    }

    DeviceSlot* getByIndex(uint8_t index) {
        if (index >= MAX_DEVICES || !devices[index].inUse) {
            return nullptr;
        }
        return &devices[index];
    }

    bool updateFrame(const char* hostname, const MetricsFrameV2& frame, unsigned long nowMs) {
        DeviceSlot* slot = getByHostname(hostname);
        if (!slot) {
            slot = allocateSlot(hostname);
            if (!slot) {
                return false;
            }
            slot->frame = frame;
            slot->online = true;
            slot->lastUpdateMs = nowMs;
            slot->dirtyMask = DIRTY_ALL;
            return true;
        }

        uint16_t dirty = DIRTY_NONE;
        if (memcmp(&slot->frame, &frame, sizeof(MetricsFrameV2)) != 0) {
            if (slot->frame.cpuPctX10 != frame.cpuPctX10 ||
                slot->frame.cpuTempCX10 != frame.cpuTempCX10) {
                dirty |= DIRTY_CPU;
            }
            if (slot->frame.ramPctX10 != frame.ramPctX10 ||
                slot->frame.ramUsedMB != frame.ramUsedMB ||
                slot->frame.ramTotalMB != frame.ramTotalMB) {
                dirty |= DIRTY_RAM;
            }
            if (slot->frame.gpuPctX10 != frame.gpuPctX10 ||
                slot->frame.gpuTempCX10 != frame.gpuTempCX10 ||
                slot->frame.gpuMemPctX10 != frame.gpuMemPctX10 ||
                slot->frame.gpuHotspotCX10 != frame.gpuHotspotCX10 ||
                slot->frame.gpuMemTempCX10 != frame.gpuMemTempCX10) {
                dirty |= DIRTY_GPU;
            }
            if (slot->frame.netRxKbps != frame.netRxKbps ||
                slot->frame.netTxKbps != frame.netTxKbps) {
                dirty |= DIRTY_NET;
            }
            if (slot->frame.diskReadKBps != frame.diskReadKBps ||
                slot->frame.diskWriteKBps != frame.diskWriteKBps) {
                dirty |= DIRTY_DISK;
            }

            slot->frame = frame;
        }

        if (!slot->online) {
            slot->online = true;
            dirty |= DIRTY_ONLINE;
        }

        slot->lastUpdateMs = nowMs;
        slot->dirtyMask |= dirty;
        return true;
    }

    void markOfflineExpired(unsigned long nowMs, unsigned long timeoutMs) {
        for (uint8_t i = 0; i < MAX_DEVICES; i++) {
            DeviceSlot& slot = devices[i];
            if (!slot.inUse || !slot.online) {
                continue;
            }
            if (nowMs - slot.lastUpdateMs > timeoutMs) {
                slot.online = false;
                slot.dirtyMask |= DIRTY_ONLINE;
            }
        }
    }

    uint16_t consumeDirtyMask(DeviceSlot* slot) {
        if (!slot) {
            return DIRTY_NONE;
        }
        uint16_t mask = slot->dirtyMask;
        slot->dirtyMask = DIRTY_NONE;
        return mask;
    }

    uint8_t getOnlineCount(MonitorConfigManager* configMgr = nullptr) {
        uint8_t count = 0;
        for (uint8_t i = 0; i < MAX_DEVICES; i++) {
            if (!devices[i].inUse || !devices[i].online) {
                continue;
            }
            if (!isDeviceEnabled(configMgr, devices[i].hostname)) {
                continue;
            }
            count++;
        }
        return count;
    }

    DeviceSlot* getOnlineByIndex(uint8_t index, MonitorConfigManager* configMgr = nullptr) {
        uint8_t cursor = 0;
        for (uint8_t i = 0; i < MAX_DEVICES; i++) {
            if (!devices[i].inUse || !devices[i].online) {
                continue;
            }
            if (!isDeviceEnabled(configMgr, devices[i].hostname)) {
                continue;
            }
            if (cursor == index) {
                return &devices[i];
            }
            cursor++;
        }
        return nullptr;
    }

private:
    DeviceSlot* allocateSlot(const char* hostname) {
        if (!hostname || hostname[0] == '\0') {
            return nullptr;
        }

        for (uint8_t i = 0; i < MAX_DEVICES; i++) {
            if (!devices[i].inUse) {
                DeviceSlot& slot = devices[i];
                slot.inUse = true;
                slot.online = false;
                slot.lastUpdateMs = 0;
                slot.dirtyMask = DIRTY_ALL;
                strlcpy(slot.hostname, hostname, sizeof(slot.hostname));
                slot.frame = MetricsFrameV2{};
                deviceCount++;
                return &slot;
            }
        }
        return nullptr;
    }

    bool isDeviceEnabled(MonitorConfigManager* configMgr, const char* hostname) {
        if (!configMgr || !hostname) {
            return true;
        }

        for (uint8_t i = 0; i < configMgr->config.deviceCount; i++) {
            if (strcmp(configMgr->config.devices[i].hostname, hostname) == 0) {
                return configMgr->config.devices[i].enabled;
            }
        }
        return true;
    }
};

#endif
