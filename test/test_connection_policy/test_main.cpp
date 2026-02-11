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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_backoff_increases_and_caps);
    RUN_TEST(test_wifi_credential_validation);
    RUN_TEST(test_mqtt_port_validation);
    return UNITY_END();
}
