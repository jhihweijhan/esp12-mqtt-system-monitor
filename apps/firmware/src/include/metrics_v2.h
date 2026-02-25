#ifndef METRICS_V2_H
#define METRICS_V2_H

#include <Arduino.h>

static const uint8_t METRICS_SCHEMA_V2 = 2;

enum MetricDirtyMask : uint16_t {
    DIRTY_NONE = 0,
    DIRTY_CPU = 1 << 0,
    DIRTY_RAM = 1 << 1,
    DIRTY_GPU = 1 << 2,
    DIRTY_NET = 1 << 3,
    DIRTY_DISK = 1 << 4,
    DIRTY_ONLINE = 1 << 5,
    DIRTY_ALL = 0xFFFF
};

struct MetricsFrameV2 {
    uint8_t version = METRICS_SCHEMA_V2;
    uint32_t senderTsMs = 0;

    // x10 scale for percentages and temperature.
    int16_t cpuPctX10 = 0;
    int16_t cpuTempCX10 = 0;

    int16_t ramPctX10 = 0;
    uint16_t ramUsedMB = 0;
    uint16_t ramTotalMB = 0;

    int16_t gpuPctX10 = 0;
    int16_t gpuTempCX10 = 0;
    int16_t gpuMemPctX10 = 0;
    int16_t gpuHotspotCX10 = 0;
    int16_t gpuMemTempCX10 = 0;

    uint16_t netRxKbps = 0;
    uint16_t netTxKbps = 0;

    uint16_t diskReadKBps = 0;
    uint16_t diskWriteKBps = 0;
};

inline uint16_t clampU16(long value) {
    if (value < 0) {
        return 0;
    }
    if (value > 65535L) {
        return 65535U;
    }
    return (uint16_t)value;
}

inline int16_t clampI16(long value) {
    if (value < -32768L) {
        return -32768;
    }
    if (value > 32767L) {
        return 32767;
    }
    return (int16_t)value;
}

inline long scaleX10(float value) {
    return lroundf(value * 10.0f);
}

inline int roundedPercent(int16_t pctX10) {
    return (pctX10 + 5) / 10;
}

inline int roundedTempC(int16_t tempX10) {
    return (tempX10 + 5) / 10;
}

inline float kbpsToMbps(uint16_t kbps) {
    return (float)kbps / 1024.0f;
}

inline float kbpsToMBps(uint16_t kBps) {
    return (float)kBps / 1024.0f;
}

#endif
