#ifndef MQTT_TRANSPORT_H
#define MQTT_TRANSPORT_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "connection_policy.h"
#include "device_store.h"
#include "metrics_parser_v2.h"
#include "monitor_config.h"

class MQTTTransport;
static MQTTTransport* _mqttTransportInstance = nullptr;

class MQTTTransport {
public:
    typedef void (*MetricsCallback)(const char* hostname);

    MQTTTransport() : _client(_wifiClient) {
        _mqttTransportInstance = this;
    }

    MetricsCallback onMetricsReceived = nullptr;

    void begin(MonitorConfigManager& configMgr, DeviceStore& store) {
        _configMgr = &configMgr;
        _store = &store;
    }

    void connect() {
        if (!_configMgr || strlen(_configMgr->config.mqttServer) == 0) {
            Serial.println("MQTT server not configured");
            return;
        }

        _client.setServer(_configMgr->config.mqttServer, _configMgr->config.mqttPort);
        _client.setCallback(mqttCallback);
        _client.setBufferSize(MQTT_MAX_PAYLOAD_BYTES);

        _reconnectFailureCount = 0;
        _nextReconnectAt = 0;
        reconnect();
    }

    void loop() {
        if (!_configMgr || !_store || strlen(_configMgr->config.mqttServer) == 0) {
            return;
        }

        unsigned long now = millis();

        if (!_client.connected()) {
            connected = false;
            if (_nextReconnectAt == 0 || (long)(now - _nextReconnectAt) >= 0) {
                reconnect();
            }
        } else {
            _client.loop();
        }

        unsigned long offlineCheckNow = millis();
        _store->markOfflineExpired(offlineCheckNow, getOfflineTimeoutMs());
    }

    bool isConnected() {
        return _client.connected();
    }

    bool isConnectedForDisplay() {
        unsigned long now = millis();
        return !shouldShowMqttDisconnectedStatus(_client.connected(), now, _lastConnectedAt, _lastMessageAt);
    }

    bool isTopicInAllowlist(const char* topic) const {
        if (!_configMgr || !topic) {
            return false;
        }

        for (uint8_t i = 0; i < _configMgr->config.subscribedTopicCount; i++) {
            if (strcmp(_configMgr->config.subscribedTopics[i], topic) == 0) {
                return true;
            }
        }
        return false;
    }

    bool hasTopicAllowlist() const {
        return _configMgr && _configMgr->config.subscribedTopicCount > 0;
    }

    void handleMessage(char* topic, uint8_t* payload, unsigned int length) {
        if (!_configMgr || !_store) {
            return;
        }

        if (!isValidMqttPayloadLength(length)) {
            Serial.printf("MQTT payload rejected: %u bytes\n", length);
            return;
        }

        bool allowlistMode = hasTopicAllowlist();
        if (allowlistMode && !isTopicInAllowlist(topic)) {
            return;
        }
        if (!allowlistMode && !isValidSenderMetricsTopic(topic)) {
            return;
        }

        char hostname[32];
        MetricsFrameV2 frame;
        if (!parseMetricsV2Payload(topic, payload, length, hostname, sizeof(hostname), frame)) {
            Serial.printf("Drop invalid metrics v2 payload on topic: %s\n", topic ? topic : "<null>");
            return;
        }

        bool isKnown = false;
        bool enabled = false;
        getDeviceConfigState(hostname, isKnown, enabled);

        if (_configMgr && isKnown && !enabled &&
            shouldAutoEnableDeviceOnSubscribedTopic(_configMgr->config.subscribedTopicCount) &&
            isTopicInAllowlist(topic)) {
            DeviceConfig* cfg = _configMgr->getOrCreateDevice(hostname);
            if (cfg && !cfg->enabled) {
                cfg->enabled = true;
                _configMgr->markDirty();
                enabled = true;
            }
        }

        if (isKnown && !enabled) {
            return;
        }

        DeviceConfig* cfg = _configMgr->getOrCreateDevice(hostname);
        if (cfg && !cfg->enabled &&
            (!allowlistMode || shouldAutoEnableDeviceOnSubscribedTopic(_configMgr->config.subscribedTopicCount))) {
            cfg->enabled = true;
            _configMgr->markDirty();
        }

        unsigned long now = millis();
        if (!_store->updateFrame(hostname, frame, now)) {
            Serial.println("Drop metrics: device store is full");
            return;
        }

        _lastMessageAt = now;
        _rxMessageCount++;

        if (now - _lastRxLogAt >= MQTT_RX_LOG_INTERVAL_MS) {
            Serial.printf("MQTT rx v2: %u msgs / %ums, last=%s\n",
                          _rxMessageCount,
                          (unsigned int)MQTT_RX_LOG_INTERVAL_MS,
                          hostname);
            _rxMessageCount = 0;
            _lastRxLogAt = now;
        }

        if (onMetricsReceived) {
            onMetricsReceived(hostname);
        }
    }

private:
    WiFiClient _wifiClient;
    PubSubClient _client;
    MonitorConfigManager* _configMgr = nullptr;
    DeviceStore* _store = nullptr;
    unsigned long _nextReconnectAt = 0;
    uint8_t _reconnectFailureCount = 0;
    unsigned long _lastRxLogAt = 0;
    uint16_t _rxMessageCount = 0;
    unsigned long _lastConnectedAt = 0;
    unsigned long _lastMessageAt = 0;
    bool connected = false;

    unsigned long getOfflineTimeoutMs() const {
        if (!_configMgr) {
            return 30000;
        }

        uint16_t sec = _configMgr->config.offlineTimeoutSec;
        if (sec < MIN_OFFLINE_TIMEOUT_SEC) {
            sec = MIN_OFFLINE_TIMEOUT_SEC;
        }
        if (sec > MAX_OFFLINE_TIMEOUT_SEC) {
            sec = MAX_OFFLINE_TIMEOUT_SEC;
        }
        return (unsigned long)sec * 1000UL;
    }

    static void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
        if (_mqttTransportInstance) {
            _mqttTransportInstance->handleMessage(topic, payload, length);
        }
    }

    void getDeviceConfigState(const char* hostname, bool& isKnown, bool& enabled) {
        isKnown = false;
        enabled = false;
        if (!_configMgr) {
            return;
        }

        for (uint8_t i = 0; i < _configMgr->config.deviceCount; i++) {
            if (strcmp(_configMgr->config.devices[i].hostname, hostname) == 0) {
                isKnown = true;
                enabled = _configMgr->config.devices[i].enabled;
                return;
            }
        }
    }

    void subscribeConfiguredTopics() {
        if (!_configMgr) {
            return;
        }

        char uniqueTopics[MAX_SUBSCRIBED_TOPICS][64];
        uint8_t uniqueCount = 0;

        for (uint8_t i = 0; i < _configMgr->config.subscribedTopicCount; i++) {
            const char* topic = _configMgr->config.subscribedTopics[i];
            if (!isValidSenderMetricsTopic(topic)) {
                Serial.printf("Skip invalid sender topic: %s\n", topic);
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < uniqueCount; j++) {
                if (strcmp(uniqueTopics[j], topic) == 0) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                continue;
            }

            if (uniqueCount >= MAX_SUBSCRIBED_TOPICS) {
                Serial.println("Skip sender topic: allowlist full");
                break;
            }

            strlcpy(uniqueTopics[uniqueCount], topic, sizeof(uniqueTopics[uniqueCount]));
            uniqueCount++;
        }

        if (!shouldSubscribeAnySenderTopic(uniqueCount)) {
            const char* discoveryTopic = _configMgr->config.mqttTopic;
            if (!isValidSenderWildcardMetricsTopic(discoveryTopic)) {
                discoveryTopic = MQTT_SENDER_DISCOVERY_TOPIC;
            }

            if (_client.subscribe(discoveryTopic)) {
                Serial.printf("Subscribed discovery topic: %s\n", discoveryTopic);
            } else {
                Serial.printf("Subscribe failed (discovery): %s\n", discoveryTopic);
            }
            return;
        }

        for (uint8_t i = 0; i < uniqueCount; i++) {
            if (_client.subscribe(uniqueTopics[i])) {
                Serial.printf("Subscribed sender topic: %s\n", uniqueTopics[i]);
            } else {
                Serial.printf("Subscribe failed: %s\n", uniqueTopics[i]);
            }
        }
    }

    void reconnect() {
        if (!_configMgr) {
            return;
        }

        Serial.printf("Connecting MQTT: %s:%d\n", _configMgr->config.mqttServer, _configMgr->config.mqttPort);

        String clientId = "ESP12-v2-" + String(random(0xffff), HEX);
        bool success = false;
        if (strlen(_configMgr->config.mqttUser) > 0) {
            success = _client.connect(clientId.c_str(), _configMgr->config.mqttUser, _configMgr->config.mqttPass);
        } else {
            success = _client.connect(clientId.c_str());
        }

        if (success) {
            connected = true;
            _reconnectFailureCount = 0;
            _nextReconnectAt = 0;
            _lastConnectedAt = millis();
            Serial.println("MQTT connected");
            subscribeConfiguredTopics();
            return;
        }

        connected = false;
        if (_reconnectFailureCount < 250) {
            _reconnectFailureCount++;
        }

        uint32_t delayMs = computeMqttReconnectDelayMs(_reconnectFailureCount);
        uint32_t jitter = random(0, 500);
        _nextReconnectAt = millis() + delayMs + jitter;

        Serial.printf("MQTT failed, rc=%d, retry in %lu ms\n", _client.state(), (unsigned long)(delayMs + jitter));
    }
};

#endif
