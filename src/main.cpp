#include <Arduino.h>
#include "include/tft_driver.h"
#include "include/qr_display.h"
#include "include/wifi_manager.h"
#include "include/web_server.h"
#include "include/monitor_config.h"
#include "include/mqtt_client.h"
#include "include/monitor_display.h"

TFTDriver tft;
QRDisplay qr(tft);
WiFiManager wifiMgr;
WebServerManager* webServer = nullptr;
MonitorConfigManager monitorConfig;
MQTTClient mqttClient;
MonitorDisplay* monitorDisplay = nullptr;

void onMqttMetricsReceived(const char* hostname) {
    if (monitorDisplay) {
        monitorDisplay->notifyMetricsUpdated(hostname);
    }
}

enum AppMode {
    MODE_AP_SETUP,      // AP 模式設定 WiFi
    MODE_MONITOR        // 監控模式
};

AppMode currentMode = MODE_AP_SETUP;

enum StartupState {
    STARTUP_INIT = 0,
    STARTUP_TRY_SAVED_START,
    STARTUP_TRY_SAVED_WAIT,
    STARTUP_TRY_SAVED_DELAY,
    STARTUP_TRY_SDK_START,
    STARTUP_TRY_SDK_WAIT,
    STARTUP_TRY_SDK_DELAY,
    STARTUP_WIFI_CONNECTED_DELAY,
    STARTUP_ENTER_AP,
    STARTUP_DONE
};

StartupState startupState = STARTUP_INIT;
uint8_t savedConnectAttempts = 0;
uint8_t sdkConnectAttempts = 0;
uint8_t startupRecoveryCycles = 0;
unsigned long startupNextAt = 0;
bool hasSavedWiFiConfig = false;
bool wifiStorageReady = false;

const uint8_t MAX_SAVED_CONNECT_ATTEMPTS = 3;
const uint8_t MAX_SDK_CONNECT_ATTEMPTS = 2;

void scheduleStartupRetryCycle() {
    startupRecoveryCycles++;
    savedConnectAttempts = 0;
    sdkConnectAttempts = 0;
    startupNextAt = millis() + 2000;
    startupState = STARTUP_TRY_SAVED_DELAY;
    Serial.printf("WiFi retries exhausted, cycle %u/%u\n",
                  startupRecoveryCycles, MAX_WIFI_RECOVERY_CYCLES_WITH_SAVED_CONFIG);
}

// 顯示 AP 模式畫面
void showAPScreen() {
    tft.fillScreen(COLOR_BLACK);

    // 標題
    tft.drawStringCentered(10, "WiFi Setup", COLOR_CYAN, COLOR_BLACK, 2);

    // AP SSID
    String apSSID = wifiMgr.getAPSSID();
    tft.drawStringCentered(45, apSSID.c_str(), COLOR_WHITE, COLOR_BLACK, 1);

    // QR Code (WiFi 連線)
    qr.drawWiFiQR(apSSID.c_str(), nullptr, 10);

    // IP 位址
    tft.drawStringCentered(210, wifiMgr.localIP.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
}

// 顯示已連線畫面（過渡畫面）
void showConnectedScreen() {
    tft.fillScreen(COLOR_BLACK);

    // 標題
    tft.drawStringCentered(10, "Connected", COLOR_GREEN, COLOR_BLACK, 2);

    // SSID
    tft.drawStringCentered(45, wifiMgr.ssid.c_str(), COLOR_WHITE, COLOR_BLACK, 1);

    // QR Code (WebUI URL)
    String url = "http://" + wifiMgr.localIP + "/monitor";
    qr.drawURLQR(url.c_str(), 10);

    // IP 位址
    tft.drawStringCentered(210, wifiMgr.localIP.c_str(), COLOR_YELLOW, COLOR_BLACK, 1);
}

// 顯示連線中畫面
void showConnectingScreen() {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(100, "Connecting", COLOR_CYAN, COLOR_BLACK, 2);
    tft.drawStringCentered(130, wifiMgr.ssid.c_str(), COLOR_WHITE, COLOR_BLACK, 1);
}

// 顯示 MQTT 連線中畫面
void showMQTTConnectingScreen() {
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(80, "MQTT", COLOR_CYAN, COLOR_BLACK, 2);
    tft.drawStringCentered(110, "Connecting...", COLOR_WHITE, COLOR_BLACK, 1);
    tft.drawStringCentered(150, monitorConfig.config.mqttServer, COLOR_GRAY, COLOR_BLACK, 1);
}

void startMonitorMode() {
    currentMode = MODE_MONITOR;

    mqttClient.begin(monitorConfig);
    mqttClient.onMetricsReceived = onMqttMetricsReceived;
    if (strlen(monitorConfig.config.mqttServer) > 0) {
        showMQTTConnectingScreen();
        mqttClient.connect();
    }

    if (!monitorDisplay) {
        monitorDisplay = new MonitorDisplay(tft, mqttClient, monitorConfig);
        monitorDisplay->begin();
    }

    if (!webServer) {
        webServer = new WebServerManager(wifiMgr);
        webServer->setMonitorConfig(&monitorConfig);
        webServer->setMQTTClient(&mqttClient);
        webServer->begin();
    }

    Serial.println("Monitor mode started");
    Serial.printf("WebUI: http://%s/monitor\n", wifiMgr.localIP.c_str());
    startupState = STARTUP_DONE;
}

void startAPMode() {
    currentMode = MODE_AP_SETUP;
    Serial.println("Entering AP mode");

    wifiMgr.startAP();
    showAPScreen();
    wifiMgr.startScan();

    if (!webServer) {
        webServer = new WebServerManager(wifiMgr);
        webServer->setMonitorConfig(&monitorConfig);
        webServer->begin();
    }

    startupState = STARTUP_DONE;
}

void processStartup() {
    switch (startupState) {
        case STARTUP_TRY_SAVED_START:
            if (!hasSavedWiFiConfig) {
                startupState = STARTUP_TRY_SDK_START;
                break;
            }
            if (savedConnectAttempts >= MAX_SAVED_CONNECT_ATTEMPTS) {
                Serial.println("Saved WiFi found but direct connect failed");
                startupState = STARTUP_TRY_SDK_START;
                break;
            }
            savedConnectAttempts++;
            Serial.printf("WiFi connect attempt %u/%u (from /wifi.json)\n",
                          savedConnectAttempts, MAX_SAVED_CONNECT_ATTEMPTS);
            if (!wifiMgr.startConnectWiFi()) {
                startupState = STARTUP_TRY_SDK_START;
                break;
            }
            startupState = STARTUP_TRY_SAVED_WAIT;
            break;

        case STARTUP_TRY_SAVED_WAIT: {
            WiFiManager::ConnectResult result = wifiMgr.pollConnect();
            if (result == WiFiManager::CONNECT_SUCCESS) {
                startupRecoveryCycles = 0;
                showConnectedScreen();
                startupNextAt = millis() + 2000;
                startupState = STARTUP_WIFI_CONNECTED_DELAY;
            } else if (result == WiFiManager::CONNECT_TIMEOUT || result == WiFiManager::CONNECT_FAILED) {
                if (savedConnectAttempts < MAX_SAVED_CONNECT_ATTEMPTS) {
                    startupNextAt = millis() + 1000;
                    startupState = STARTUP_TRY_SAVED_DELAY;
                } else {
                    startupState = STARTUP_TRY_SDK_START;
                }
            }
            break;
        }

        case STARTUP_TRY_SAVED_DELAY:
            if ((long)(millis() - startupNextAt) >= 0) {
                startupState = STARTUP_TRY_SAVED_START;
            }
            break;

        case STARTUP_TRY_SDK_START:
            if (sdkConnectAttempts >= MAX_SDK_CONNECT_ATTEMPTS) {
                if (shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig,
                                                     wifiStorageReady,
                                                     startupRecoveryCycles)) {
                    startupState = STARTUP_ENTER_AP;
                } else {
                    scheduleStartupRetryCycle();
                }
                break;
            }
            showConnectingScreen();
            sdkConnectAttempts++;
            Serial.printf("WiFi connect attempt %u/%u (from SDK)\n",
                          sdkConnectAttempts, MAX_SDK_CONNECT_ATTEMPTS);
            if (!wifiMgr.startConnectStoredWiFi()) {
                if (shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig,
                                                     wifiStorageReady,
                                                     startupRecoveryCycles)) {
                    startupState = STARTUP_ENTER_AP;
                } else {
                    scheduleStartupRetryCycle();
                }
                break;
            }
            startupState = STARTUP_TRY_SDK_WAIT;
            break;

        case STARTUP_TRY_SDK_WAIT: {
            WiFiManager::ConnectResult result = wifiMgr.pollConnect();
            if (result == WiFiManager::CONNECT_SUCCESS) {
                startupRecoveryCycles = 0;
                showConnectedScreen();
                startupNextAt = millis() + 2000;
                startupState = STARTUP_WIFI_CONNECTED_DELAY;
            } else if (result == WiFiManager::CONNECT_TIMEOUT || result == WiFiManager::CONNECT_FAILED) {
                if (sdkConnectAttempts < MAX_SDK_CONNECT_ATTEMPTS) {
                    startupNextAt = millis() + 1000;
                    startupState = STARTUP_TRY_SDK_DELAY;
                } else {
                    if (shouldEnterApModeAfterBootRetries(hasSavedWiFiConfig,
                                                         wifiStorageReady,
                                                         startupRecoveryCycles)) {
                        startupState = STARTUP_ENTER_AP;
                    } else {
                        scheduleStartupRetryCycle();
                    }
                }
            }
            break;
        }

        case STARTUP_TRY_SDK_DELAY:
            if ((long)(millis() - startupNextAt) >= 0) {
                startupState = STARTUP_TRY_SDK_START;
            }
            break;

        case STARTUP_WIFI_CONNECTED_DELAY:
            if ((long)(millis() - startupNextAt) >= 0) {
                startMonitorMode();
            }
            break;

        case STARTUP_ENTER_AP:
            startAPMode();
            break;

        case STARTUP_DONE:
        case STARTUP_INIT:
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP12 System Monitor ===");

    // 初始化 TFT
    tft.begin();
    tft.fillScreen(COLOR_BLACK);
    tft.drawStringCentered(110, "Starting...", COLOR_WHITE, COLOR_BLACK, 2);

    // 初始化 WiFi Manager
    wifiMgr.begin();

    // 初始化監控設定
    monitorConfig.begin();
    monitorConfig.load();

    // 啟動非阻塞連線流程
    wifiStorageReady = wifiMgr.isStorageReady();
    hasSavedWiFiConfig = wifiMgr.loadConfig();
    if (hasSavedWiFiConfig) {
        showConnectingScreen();
        startupState = STARTUP_TRY_SAVED_START;
    } else if (!wifiStorageReady) {
        Serial.println("LittleFS unavailable, cannot load /wifi.json");
        showConnectingScreen();
        startupState = STARTUP_TRY_SDK_START;
    } else {
        Serial.println("No valid /wifi.json, fallback to SDK saved credentials");
        showConnectingScreen();
        startupState = STARTUP_TRY_SDK_START;
    }
}

void loop() {
    if (startupState != STARTUP_DONE) {
        processStartup();
        if (webServer) {
            webServer->loop();
        }
        yield();
        delay(2);
        return;
    }

    // Web Server 延遲重啟
    if (webServer) webServer->loop();

    if (currentMode == MODE_MONITOR) {
        // 監控模式
        mqttClient.loop();
        yield();
        monitorConfig.loop();  // 處理延遲儲存
        yield();
        if (monitorDisplay) {
            monitorDisplay->loop();
        }
    }

    yield();
    delay(2);
}
