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

enum AppMode {
    MODE_AP_SETUP,      // AP 模式設定 WiFi
    MODE_MONITOR        // 監控模式
};

AppMode currentMode = MODE_AP_SETUP;

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

    // 嘗試載入並連線 WiFi
    bool wifiConnected = false;
    if (wifiMgr.loadConfig()) {
        showConnectingScreen();

        const uint8_t maxConnectAttempts = 3;
        for (uint8_t attempt = 1; attempt <= maxConnectAttempts; attempt++) {
            Serial.printf("WiFi connect attempt %u/%u (from /wifi.json)\n", attempt, maxConnectAttempts);
            if (wifiMgr.connectWiFi()) {
                wifiConnected = true;
                break;
            }
            if (attempt < maxConnectAttempts) {
                delay(1000);
            }
        }

        if (!wifiConnected) {
            Serial.println("Saved WiFi found but direct connect failed");
        }
    } else if (!wifiMgr.isStorageReady()) {
        Serial.println("LittleFS unavailable, cannot load /wifi.json");
    } else {
        Serial.println("No valid /wifi.json, fallback to SDK saved credentials");
    }

    // 備援：即使 /wifi.json 讀不到，也嘗試用 SDK 內建保存的 WiFi 憑證連線
    if (!wifiConnected) {
        showConnectingScreen();
        const uint8_t maxSdkAttempts = 2;
        for (uint8_t attempt = 1; attempt <= maxSdkAttempts; attempt++) {
            Serial.printf("WiFi connect attempt %u/%u (from SDK)\n", attempt, maxSdkAttempts);
            if (wifiMgr.connectStoredWiFi()) {
                wifiConnected = true;
                break;
            }
            if (attempt < maxSdkAttempts) {
                delay(1000);
            }
        }
    }

    if (wifiConnected) {
        // WiFi 連線成功
        showConnectedScreen();
        delay(2000);  // 顯示連線資訊 2 秒

        // 進入監控模式
        currentMode = MODE_MONITOR;

        // 初始化 MQTT
        mqttClient.begin(monitorConfig);

        // 如果有設定 MQTT 伺服器，則連線
        if (strlen(monitorConfig.config.mqttServer) > 0) {
            showMQTTConnectingScreen();
            mqttClient.connect();
            delay(1000);
        }

        // 初始化監控顯示
        monitorDisplay = new MonitorDisplay(tft, mqttClient, monitorConfig);
        monitorDisplay->begin();

        // 啟動 Web Server
        webServer = new WebServerManager(wifiMgr);
        webServer->setMonitorConfig(&monitorConfig);
        webServer->setMQTTClient(&mqttClient);
        webServer->begin();

        Serial.println("Monitor mode started");
        Serial.printf("WebUI: http://%s/monitor\n", wifiMgr.localIP.c_str());
        return;
    }

    // 無設定或連線失敗 - 進入 AP 模式
    currentMode = MODE_AP_SETUP;
    Serial.println("Entering AP mode");
    wifiMgr.startAP();
    showAPScreen();

    // 開始背景掃描 WiFi
    wifiMgr.startScan();

    // 啟動 Web Server
    webServer = new WebServerManager(wifiMgr);
    webServer->setMonitorConfig(&monitorConfig);
    webServer->begin();
}

void loop() {
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
    delay(10);
}
