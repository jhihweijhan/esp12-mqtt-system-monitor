#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#define WIFI_CONFIG_FILE "/wifi.json"
#define WIFI_CONNECT_TIMEOUT 10000  // 10 秒
#define AP_IP IPAddress(192, 168, 4, 1)

class WiFiManager {
public:
    String ssid;
    String password;
    String deviceID;
    String localIP;
    bool isAPMode = false;

    void begin() {
        // 取得設備 ID (MAC 後 6 碼)
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char id[7];
        snprintf(id, sizeof(id), "%02X%02X%02X", mac[3], mac[4], mac[5]);
        deviceID = String(id);

        // 初始化 LittleFS
        if (!LittleFS.begin()) {
            Serial.println("LittleFS 初始化失敗");
        }
    }

    bool loadConfig() {
        if (!LittleFS.exists(WIFI_CONFIG_FILE)) {
            Serial.println("設定檔不存在");
            return false;
        }

        File file = LittleFS.open(WIFI_CONFIG_FILE, "r");
        if (!file) {
            Serial.println("無法開啟設定檔");
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.println("JSON 解析失敗");
            return false;
        }

        ssid = doc["ssid"].as<String>();
        password = doc["pass"].as<String>();

        Serial.printf("載入設定: SSID=%s\n", ssid.c_str());
        return ssid.length() > 0;
    }

    bool saveConfig(const String& newSSID, const String& newPass) {
        JsonDocument doc;
        doc["ssid"] = newSSID;
        doc["pass"] = newPass;

        File file = LittleFS.open(WIFI_CONFIG_FILE, "w");
        if (!file) {
            Serial.println("無法寫入設定檔");
            return false;
        }

        serializeJson(doc, file);
        file.close();

        ssid = newSSID;
        password = newPass;

        Serial.printf("儲存設定: SSID=%s\n", ssid.c_str());
        return true;
    }

    bool connectWiFi() {
        if (ssid.length() == 0) return false;

        Serial.printf("連線到 WiFi: %s\n", ssid.c_str());

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startTime > WIFI_CONNECT_TIMEOUT) {
                Serial.println("WiFi 連線超時");
                return false;
            }
            delay(500);
            Serial.print(".");
        }

        localIP = WiFi.localIP().toString();
        isAPMode = false;

        Serial.printf("\nWiFi 已連線! IP: %s\n", localIP.c_str());
        return true;
    }

    void startAP() {
        String apSSID = "ESP12-" + deviceID;

        Serial.printf("啟動 AP 模式: %s\n", apSSID.c_str());

        // 使用 AP+STA 模式，這樣才能同時掃描 WiFi
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
        WiFi.softAP(apSSID.c_str());

        localIP = WiFi.softAPIP().toString();
        isAPMode = true;

        Serial.printf("AP 已啟動! IP: %s\n", localIP.c_str());
    }

    String getAPSSID() {
        return "ESP12-" + deviceID;
    }

    int scanState = -2;  // -2=未開始, -1=掃描中, >=0=完成

    void startScan() {
        Serial.println("開始非同步掃描...");
        WiFi.scanNetworks(true);  // async = true
        scanState = -1;
    }

    String getScanResults() {
        int n = WiFi.scanComplete();

        if (n == WIFI_SCAN_RUNNING) {
            return "{\"scanning\":true}";
        }

        if (n == WIFI_SCAN_FAILED || n < 0) {
            WiFi.scanDelete();
            startScan();
            return "{\"scanning\":true}";
        }

        Serial.printf("找到 %d 個網路\n", n);

        JsonDocument doc;
        JsonArray networks = doc.to<JsonArray>();

        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 0) {  // 過濾空 SSID
                JsonObject net = networks.add<JsonObject>();
                net["ssid"] = ssid;
                net["rssi"] = WiFi.RSSI(i);
                net["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
                Serial.printf("  %s (%d dBm)\n", ssid.c_str(), WiFi.RSSI(i));
            }
        }

        WiFi.scanDelete();

        String result;
        serializeJson(doc, result);
        return result;
    }
};

#endif
