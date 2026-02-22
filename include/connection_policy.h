#ifndef CONNECTION_POLICY_H
#define CONNECTION_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint32_t MQTT_RECONNECT_BASE_MS = 1000UL;
static const uint32_t MQTT_RECONNECT_MAX_MS = 60000UL;
static const size_t MQTT_MAX_PAYLOAD_BYTES = 8192U;
static const uint16_t MQTT_CONNECT_SOCKET_TIMEOUT_SEC = 2U;
static const uint16_t MQTT_SOCKET_TIMEOUT_MIN_SEC = 1U;
static const uint16_t MQTT_SOCKET_TIMEOUT_MAX_SEC = 5U;
static const uint16_t MQTT_KEEP_ALIVE_SEC = 15U;
static const uint32_t WIFI_RECONNECT_RETRY_MS = 5000UL;
static const size_t WIFI_MAX_SSID_LENGTH = 32U;
static const size_t WIFI_MAX_PASSWORD_LENGTH = 63U;
static const uint8_t MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG = 12U;
static const uint16_t DISPLAY_IDLE_REFRESH_MS = 1000U;
static const uint16_t DISPLAY_ACTIVE_REFRESH_MS = 250U;
static const uint16_t DISPLAY_FORCE_REDRAW_REFRESH_MS = 120U;
static const uint16_t MQTT_RX_LOG_INTERVAL_MS = 2000U;
static const uint16_t MQTT_STATUS_DISCONNECT_GRACE_MS = 5000U;

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

static inline uint16_t sanitizeMqttSocketTimeoutSec(uint16_t timeoutSec) {
    if (timeoutSec < MQTT_SOCKET_TIMEOUT_MIN_SEC) {
        return MQTT_SOCKET_TIMEOUT_MIN_SEC;
    }
    if (timeoutSec > MQTT_SOCKET_TIMEOUT_MAX_SEC) {
        return MQTT_SOCKET_TIMEOUT_MAX_SEC;
    }
    return timeoutSec;
}

static inline bool shouldAttemptWifiReconnect(unsigned long nowMs,
                                              unsigned long lastWifiReconnectAtMs,
                                              uint32_t retryIntervalMs = WIFI_RECONNECT_RETRY_MS) {
    return (nowMs - lastWifiReconnectAtMs) >= retryIntervalMs;
}

static inline bool shouldEnterApModeAfterBootRetries(bool hasSavedWiFiConfig,
                                                     bool storageReady,
                                                     uint8_t recoveryCycles) {
    if (hasSavedWiFiConfig) {
        return recoveryCycles >= MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG;
    }

    // LittleFS 暫時不可用時，先保守重試，避免誤判後直接進 AP
    if (!storageReady) {
        return recoveryCycles >= MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG;
    }

    return true;
}

static inline bool shouldEnterApModeAfterBootRetries(bool hasSavedWiFiConfig, uint8_t recoveryCycles) {
    return shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig, true, recoveryCycles);
}

static inline bool shouldUseWildcardMqttSubscription(uint8_t enabledDeviceCount) {
    return enabledDeviceCount == 0;
}

static inline bool shouldSubscribeAnySenderTopic(uint8_t topicCount) {
    return topicCount > 0;
}

static inline bool shouldAutoEnableDeviceOnSubscribedTopic(uint8_t topicCount) {
    return topicCount > 0;
}

static inline bool isValidSenderMetricsTopic(const char* topic) {
    if (!topic) {
        return false;
    }

    static const char PREFIX[] = "sys/agents/";
    static const char SUFFIX[] = "/metrics";

    size_t topicLen = strlen(topic);
    size_t prefixLen = sizeof(PREFIX) - 1;
    size_t suffixLen = sizeof(SUFFIX) - 1;
    if (topicLen <= prefixLen + suffixLen) {
        return false;
    }

    if (strncmp(topic, PREFIX, prefixLen) != 0) {
        return false;
    }

    if (strcmp(topic + (topicLen - suffixLen), SUFFIX) != 0) {
        return false;
    }

    const char* hostStart = topic + prefixLen;
    const char* hostEnd = topic + (topicLen - suffixLen);
    if (hostStart >= hostEnd) {
        return false;
    }

    for (const char* p = hostStart; p < hostEnd; ++p) {
        char c = *p;
        if (c == '\0' || c == '/' || c == '+' || c == '#') {
            return false;
        }
    }

    return true;
}

static inline uint16_t computeDisplayRefreshIntervalMs(bool hasPendingVisibleUpdate, bool forceRedraw) {
    if (forceRedraw) {
        return DISPLAY_FORCE_REDRAW_REFRESH_MS;
    }
    return hasPendingVisibleUpdate ? DISPLAY_ACTIVE_REFRESH_MS : DISPLAY_IDLE_REFRESH_MS;
}

static inline bool shouldShowMqttDisconnectedStatus(bool socketConnected,
                                                    unsigned long nowMs,
                                                    unsigned long lastConnectedAtMs,
                                                    unsigned long lastMessageAtMs) {
    if (socketConnected) {
        return false;
    }

    if (nowMs - lastConnectedAtMs < MQTT_STATUS_DISCONNECT_GRACE_MS) {
        return false;
    }

    if (nowMs - lastMessageAtMs < MQTT_STATUS_DISCONNECT_GRACE_MS) {
        return false;
    }

    return true;
}

#endif
