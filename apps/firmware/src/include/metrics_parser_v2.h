#ifndef METRICS_PARSER_V2_H
#define METRICS_PARSER_V2_H

#include <Arduino.h>
#include <ArduinoJson.h>

#include "connection_policy.h"
#include "metrics_v2.h"

inline bool parseScaledX10(JsonVariantConst value, int16_t& out) {
    if (value.isNull()) {
        return false;
    }
    float f = value.as<float>();
    out = clampI16(scaleX10(f));
    return true;
}

inline bool parseU16Value(JsonVariantConst value, uint16_t& out) {
    if (value.isNull()) {
        return false;
    }
    float f = value.as<float>();
    out = clampU16(lroundf(f));
    return true;
}

inline bool parseMetricsV2Payload(const char* topic,
                                  const uint8_t* payload,
                                  size_t length,
                                  char* hostname,
                                  size_t hostnameSize,
                                  MetricsFrameV2& frame) {
    if (!payload || !hostname || hostnameSize == 0) {
        return false;
    }

    if (!extractHostnameFromSenderTopic(topic, hostname, hostnameSize)) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        return false;
    }

    uint8_t version = doc["v"] | 0;
    if (version != METRICS_SCHEMA_V2) {
        return false;
    }

    frame.version = version;
    frame.senderTsMs = doc["ts"] | 0;

    JsonArrayConst cpu = doc["cpu"].as<JsonArrayConst>();
    if (!cpu.isNull()) {
        if (cpu.size() > 0) {
            parseScaledX10(cpu[0], frame.cpuPctX10);
        }
        if (cpu.size() > 1) {
            parseScaledX10(cpu[1], frame.cpuTempCX10);
        }
    }

    JsonArrayConst ram = doc["ram"].as<JsonArrayConst>();
    if (!ram.isNull()) {
        if (ram.size() > 0) {
            parseScaledX10(ram[0], frame.ramPctX10);
        }
        if (ram.size() > 1) {
            parseU16Value(ram[1], frame.ramUsedMB);
        }
        if (ram.size() > 2) {
            parseU16Value(ram[2], frame.ramTotalMB);
        }
    }

    JsonArrayConst gpu = doc["gpu"].as<JsonArrayConst>();
    if (!gpu.isNull()) {
        if (gpu.size() > 0) {
            parseScaledX10(gpu[0], frame.gpuPctX10);
        }
        if (gpu.size() > 1) {
            parseScaledX10(gpu[1], frame.gpuTempCX10);
        }
        if (gpu.size() > 2) {
            parseScaledX10(gpu[2], frame.gpuMemPctX10);
        }
        if (gpu.size() > 3) {
            parseScaledX10(gpu[3], frame.gpuHotspotCX10);
        }
        if (gpu.size() > 4) {
            parseScaledX10(gpu[4], frame.gpuMemTempCX10);
        }
    }

    JsonArrayConst net = doc["net"].as<JsonArrayConst>();
    if (!net.isNull()) {
        if (net.size() > 0) {
            parseU16Value(net[0], frame.netRxKbps);
        }
        if (net.size() > 1) {
            parseU16Value(net[1], frame.netTxKbps);
        }
    }

    JsonArrayConst disk = doc["disk"].as<JsonArrayConst>();
    if (!disk.isNull()) {
        if (disk.size() > 0) {
            parseU16Value(disk[0], frame.diskReadKBps);
        }
        if (disk.size() > 1) {
            parseU16Value(disk[1], frame.diskWriteKBps);
        }
    }

    return true;
}

#endif
