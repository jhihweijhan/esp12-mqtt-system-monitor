#include <unity.h>

#include "connection_policy.h"

void test_backoff_increases_and_caps() {
    TEST_ASSERT_EQUAL_UINT32(1000UL, computeMqttReconnectDelayMs(0));
    TEST_ASSERT_EQUAL_UINT32(2000UL, computeMqttReconnectDelayMs(1));
    TEST_ASSERT_EQUAL_UINT32(32000UL, computeMqttReconnectDelayMs(5));
    TEST_ASSERT_EQUAL_UINT32(60000UL, computeMqttReconnectDelayMs(8));
}

void test_wifi_credential_validation() {
    TEST_ASSERT_TRUE(isValidWifiCredentialLength(1, 0));
    TEST_ASSERT_TRUE(isValidWifiCredentialLength(32, 63));
    TEST_ASSERT_FALSE(isValidWifiCredentialLength(0, 8));
    TEST_ASSERT_FALSE(isValidWifiCredentialLength(33, 8));
    TEST_ASSERT_FALSE(isValidWifiCredentialLength(10, 64));
}

void test_mqtt_port_validation() {
    TEST_ASSERT_TRUE(isValidMqttPort(1883));
    TEST_ASSERT_TRUE(isValidMqttPort(8883));
    TEST_ASSERT_FALSE(isValidMqttPort(0));
}

void test_wifi_boot_ap_fallback_policy() {
    TEST_ASSERT_TRUE(shouldEnterApModeAfterBootRetries(false, true, 0));
    TEST_ASSERT_FALSE(shouldEnterApModeAfterBootRetries(true, true, 0));
    TEST_ASSERT_FALSE(shouldEnterApModeAfterBootRetries(true, true, MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG - 1));
    TEST_ASSERT_TRUE(shouldEnterApModeAfterBootRetries(true, true, MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG));

    // LittleFS 暫時不可用時，不應該立刻退回 AP；先走完整重試循環
    TEST_ASSERT_FALSE(shouldEnterApModeAfterBootRetries(false, false, 0));
    TEST_ASSERT_FALSE(shouldEnterApModeAfterBootRetries(false, false, MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG - 1));
    TEST_ASSERT_TRUE(shouldEnterApModeAfterBootRetries(false, false, MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG));
}

void test_mqtt_subscription_strategy_policy() {
    TEST_ASSERT_TRUE(shouldUseWildcardMqttSubscription(0));
    TEST_ASSERT_FALSE(shouldUseWildcardMqttSubscription(1));
    TEST_ASSERT_FALSE(shouldUseWildcardMqttSubscription(8));
}

void test_sender_topic_subscription_policy() {
    TEST_ASSERT_FALSE(shouldSubscribeAnySenderTopic(0));
    TEST_ASSERT_TRUE(shouldSubscribeAnySenderTopic(1));
    TEST_ASSERT_TRUE(shouldSubscribeAnySenderTopic(8));
}

void test_auto_enable_device_on_subscribed_topic_policy() {
    TEST_ASSERT_FALSE(shouldAutoEnableDeviceOnSubscribedTopic(0));
    TEST_ASSERT_TRUE(shouldAutoEnableDeviceOnSubscribedTopic(1));
    TEST_ASSERT_TRUE(shouldAutoEnableDeviceOnSubscribedTopic(8));
}

void test_sender_topic_validation_policy() {
    TEST_ASSERT_TRUE(isValidSenderMetricsTopic("sys/agents/desk/metrics"));
    TEST_ASSERT_TRUE(isValidSenderMetricsTopic("sys/agents/nas-01/metrics"));

    TEST_ASSERT_FALSE(isValidSenderMetricsTopic(nullptr));
    TEST_ASSERT_FALSE(isValidSenderMetricsTopic(""));
    TEST_ASSERT_FALSE(isValidSenderMetricsTopic("sys/agents/+/metrics"));
    TEST_ASSERT_FALSE(isValidSenderMetricsTopic("sys/agents/#/metrics"));
    TEST_ASSERT_FALSE(isValidSenderMetricsTopic("sys/agents//metrics"));
    TEST_ASSERT_FALSE(isValidSenderMetricsTopic("sys/agents/desk/state"));
    TEST_ASSERT_FALSE(isValidSenderMetricsTopic("other/agents/desk/metrics"));
}

void test_display_refresh_policy() {
    TEST_ASSERT_EQUAL_UINT16(DISPLAY_FORCE_REDRAW_REFRESH_MS,
                             computeDisplayRefreshIntervalMs(false, true));
    TEST_ASSERT_EQUAL_UINT16(DISPLAY_ACTIVE_REFRESH_MS,
                             computeDisplayRefreshIntervalMs(true, false));
    TEST_ASSERT_EQUAL_UINT16(DISPLAY_IDLE_REFRESH_MS,
                             computeDisplayRefreshIntervalMs(false, false));
}

void test_mqtt_disconnect_status_grace_policy() {
    TEST_ASSERT_FALSE(shouldShowMqttDisconnectedStatus(true, 10000, 0, 0));
    TEST_ASSERT_FALSE(shouldShowMqttDisconnectedStatus(false, 10000, 7000, 0));
    TEST_ASSERT_FALSE(shouldShowMqttDisconnectedStatus(false, 10000, 0, 7000));
    TEST_ASSERT_TRUE(shouldShowMqttDisconnectedStatus(false, 10000, 1000, 1000));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_backoff_increases_and_caps);
    RUN_TEST(test_wifi_credential_validation);
    RUN_TEST(test_mqtt_port_validation);
    RUN_TEST(test_wifi_boot_ap_fallback_policy);
    RUN_TEST(test_mqtt_subscription_strategy_policy);
    RUN_TEST(test_sender_topic_subscription_policy);
    RUN_TEST(test_auto_enable_device_on_subscribed_topic_policy);
    RUN_TEST(test_sender_topic_validation_policy);
    RUN_TEST(test_display_refresh_policy);
    RUN_TEST(test_mqtt_disconnect_status_grace_policy);
    return UNITY_END();
}
