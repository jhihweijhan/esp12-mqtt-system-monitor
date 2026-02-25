#ifndef CONNECTION_POLICY_H
#define CONNECTION_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint32_t MQTT_RECONNECT_BASE_MS = 1000UL;
static const uint32_t MQTT_RECONNECT_MAX_MS = 60000UL;
static const size_t MQTT_MAX_PAYLOAD_BYTES = 1024U;
static const size_t WIFI_MAX_SSID_LENGTH = 32U;
static const size_t WIFI_MAX_PASSWORD_LENGTH = 63U;
static const uint8_t MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG = 12U;
static const uint16_t DISPLAY_IDLE_REFRESH_MS = 1000U;
static const uint16_t DISPLAY_ACTIVE_REFRESH_MS = 200U;
static const uint16_t DISPLAY_FORCE_REDRAW_REFRESH_MS = 90U;
static const uint16_t MQTT_RX_LOG_INTERVAL_MS = 2000U;
static const uint16_t MQTT_STATUS_DISCONNECT_GRACE_MS = 5000U;
static const uint16_t DEVICE_ONLINE_DIRTY_MASK = 1U << 5;

static const char MQTT_SENDER_TOPIC_PREFIX[] = "sys/agents/";
static const char MQTT_SENDER_TOPIC_SUFFIX[] = "/metrics/v2";
static const char MQTT_SENDER_DISCOVERY_TOPIC[] = "sys/agents/+/metrics/v2";

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

static inline bool shouldEnterApModeAfterBootRetries(bool hasSavedWiFiConfig,
                                                     bool storageReady,
                                                     uint8_t recoveryCycles) {
    if (hasSavedWiFiConfig) {
        return recoveryCycles >= MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG;
    }

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

static inline bool isValidSenderHostname(const char* hostStart, const char* hostEnd) {
    if (!hostStart || !hostEnd || hostStart >= hostEnd) {
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

static inline bool isValidSenderMetricsTopic(const char* topic) {
    if (!topic) {
        return false;
    }

    const size_t topicLen = strlen(topic);
    const size_t prefixLen = sizeof(MQTT_SENDER_TOPIC_PREFIX) - 1;
    const size_t suffixLen = sizeof(MQTT_SENDER_TOPIC_SUFFIX) - 1;

    if (topicLen <= prefixLen + suffixLen) {
        return false;
    }

    if (strncmp(topic, MQTT_SENDER_TOPIC_PREFIX, prefixLen) != 0) {
        return false;
    }

    if (strcmp(topic + topicLen - suffixLen, MQTT_SENDER_TOPIC_SUFFIX) != 0) {
        return false;
    }

    const char* hostStart = topic + prefixLen;
    const char* hostEnd = topic + topicLen - suffixLen;
    return isValidSenderHostname(hostStart, hostEnd);
}

static inline bool isValidSenderWildcardMetricsTopic(const char* topic) {
    if (!topic) {
        return false;
    }

    const size_t topicLen = strlen(topic);
    const size_t prefixLen = sizeof(MQTT_SENDER_TOPIC_PREFIX) - 1;
    const size_t suffixLen = sizeof(MQTT_SENDER_TOPIC_SUFFIX) - 1;

    if (topicLen <= prefixLen + suffixLen) {
        return false;
    }

    if (strncmp(topic, MQTT_SENDER_TOPIC_PREFIX, prefixLen) != 0) {
        return false;
    }

    if (strcmp(topic + topicLen - suffixLen, MQTT_SENDER_TOPIC_SUFFIX) != 0) {
        return false;
    }

    const char* hostStart = topic + prefixLen;
    const char* hostEnd = topic + topicLen - suffixLen;
    size_t hostLen = (size_t)(hostEnd - hostStart);

    return hostLen == 1 && hostStart[0] == '+';
}

static inline bool extractHostnameFromSenderTopic(const char* topic, char* outHost, size_t outHostSize) {
    if (!outHost || outHostSize == 0) {
        return false;
    }

    outHost[0] = '\0';
    if (!isValidSenderMetricsTopic(topic)) {
        return false;
    }

    const size_t prefixLen = sizeof(MQTT_SENDER_TOPIC_PREFIX) - 1;
    const size_t suffixLen = sizeof(MQTT_SENDER_TOPIC_SUFFIX) - 1;
    const size_t topicLen = strlen(topic);

    const char* hostStart = topic + prefixLen;
    const char* hostEnd = topic + topicLen - suffixLen;
    size_t hostLen = (size_t)(hostEnd - hostStart);

    if (hostLen >= outHostSize) {
        hostLen = outHostSize - 1;
    }

    memcpy(outHost, hostStart, hostLen);
    outHost[hostLen] = '\0';
    return hostLen > 0;
}

static inline uint16_t computeDisplayRefreshIntervalMs(bool hasPendingVisibleUpdate, bool forceRedraw) {
    if (forceRedraw) {
        return DISPLAY_FORCE_REDRAW_REFRESH_MS;
    }
    return hasPendingVisibleUpdate ? DISPLAY_ACTIVE_REFRESH_MS : DISPLAY_IDLE_REFRESH_MS;
}

static inline bool shouldRedrawDeviceHeader(bool forceRedraw,
                                            bool hostnameChanged,
                                            uint16_t dirtyMask) {
    if (forceRedraw || hostnameChanged) {
        return true;
    }
    return (dirtyMask & DEVICE_ONLINE_DIRTY_MASK) != 0;
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

static inline bool hasElapsedIntervalMs(unsigned long nowMs,
                                        unsigned long sinceMs,
                                        unsigned long intervalMs) {
    return (long)(nowMs - sinceMs) > (long)intervalMs;
}

#endif
