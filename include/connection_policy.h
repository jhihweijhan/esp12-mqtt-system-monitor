#ifndef CONNECTION_POLICY_H
#define CONNECTION_POLICY_H

#include <stddef.h>
#include <stdint.h>

static const uint32_t MQTT_RECONNECT_BASE_MS = 1000UL;
static const uint32_t MQTT_RECONNECT_MAX_MS = 60000UL;
static const size_t MQTT_MAX_PAYLOAD_BYTES = 8192U;
static const size_t WIFI_MAX_SSID_LENGTH = 32U;
static const size_t WIFI_MAX_PASSWORD_LENGTH = 63U;

static inline uint32_t computeMqttReconnectDelayMs(uint8_t failureCount) {
    if (failureCount > 31) {
        failureCount = 31;
    }
    uint32_t delayMs = MQTT_RECONNECT_BASE_MS << failureCount;
    if (delayMs > MQTT_RECONNECT_MAX_MS) {
        delayMs = MQTT_RECONNECT_MAX_MS;
    }
    return delayMs;
}

static inline bool isValidWifiCredentialLength(size_t ssidLen, size_t passLen) {
    if (ssidLen == 0 || ssidLen > WIFI_MAX_SSID_LENGTH) {
        return false;
    }
    if (passLen > WIFI_MAX_PASSWORD_LENGTH) {
        return false;
    }
    return true;
}

static inline bool isValidMqttPort(uint16_t port) {
    return port > 0;
}

static inline bool isValidMqttPayloadLength(size_t payloadLen) {
    return payloadLen > 0 && payloadLen <= MQTT_MAX_PAYLOAD_BYTES;
}

#endif
