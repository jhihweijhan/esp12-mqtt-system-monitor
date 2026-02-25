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
    enum ConnectResult {
        CONNECT_IDLE = 0,
        CONNECT_IN_PROGRESS,
        CONNECT_SUCCESS,
        CONNECT_TIMEOUT,
        CONNECT_FAILED
    };

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

        // 初始化 LittleFS（僅重試，不自動格式化，避免清空已儲存設定）
        _fsReady = LittleFS.begin();
        if (!_fsReady) {
            Serial.println("LittleFS 初始化失敗，50ms 後重試...");
            delay(50);
            _fsReady = LittleFS.begin();
        }

        if (!_fsReady) {
            Serial.println("LittleFS 無法使用，保留現有資料，不執行自動格式化");
            Serial.println("WiFi 設定將無法持久化，請手動檢查檔案系統");
        }
    }

    bool isStorageReady() const {
        return _fsReady;
    }

    bool loadConfig() {
        if (!_fsReady) {
            Serial.println("LittleFS 未就緒，無法讀取 WiFi 設定");
            return false;
        }

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
            Serial.printf("JSON 解析失敗: %s\n", error.c_str());
            return false;
        }

        ssid = doc["ssid"].as<String>();
        password = doc["pass"].as<String>();

        if (ssid.length() == 0) {
            Serial.println("設定檔 SSID 為空");
            return false;
        }

        Serial.printf("載入設定: SSID=%s\n", ssid.c_str());
        return true;
    }

    bool saveConfig(const String& newSSID, const String& newPass) {
        if (!_fsReady) {
            Serial.println("LittleFS 未就緒，無法儲存 WiFi 設定");
            return false;
        }

        if (newSSID.length() == 0) {
            Serial.println("SSID 不可為空");
            return false;
        }

        JsonDocument doc;
        doc["ssid"] = newSSID;
        doc["pass"] = newPass;

        File file = LittleFS.open(WIFI_CONFIG_FILE, "w");
        if (!file) {
            Serial.println("無法寫入設定檔");
            return false;
        }

        size_t written = serializeJson(doc, file);
        file.flush();
        file.close();

        if (written == 0) {
            Serial.println("WiFi 設定寫入失敗");
            return false;
        }

        // 立即回讀驗證，避免重開後才發現寫入內容無效
        File verifyFile = LittleFS.open(WIFI_CONFIG_FILE, "r");
        if (!verifyFile) {
            Serial.println("無法驗證設定檔");
            return false;
        }

        JsonDocument verifyDoc;
        DeserializationError verifyError = deserializeJson(verifyDoc, verifyFile);
        verifyFile.close();
        if (verifyError) {
            Serial.printf("驗證設定檔失敗: %s\n", verifyError.c_str());
            return false;
        }

        if (verifyDoc["ssid"].as<String>() != newSSID) {
            Serial.println("驗證設定檔失敗: SSID 不一致");
            return false;
        }

        ssid = newSSID;
        password = newPass;

        Serial.printf("儲存設定: SSID=%s\n", ssid.c_str());
        return true;
    }

    bool startConnectWiFi() {
        if (ssid.length() == 0) return false;

        Serial.printf("連線到 WiFi: %s\n", ssid.c_str());

        WiFi.persistent(true);
        WiFi.setAutoReconnect(true);
        WiFi.mode(WIFI_STA);
        delay(30);
        WiFi.disconnect();
        delay(80);
        int networkCount = WiFi.scanNetworks(false, true);
        int matchedIndex = -1;
        if (networkCount > 0) {
            Serial.printf("掃描到 %d 個 AP\n", networkCount);
            for (int i = 0; i < networkCount; i++) {
                String foundSsid = WiFi.SSID(i);
                int32_t rssi = WiFi.RSSI(i);
                int32_t channel = WiFi.channel(i);
                Serial.printf("  AP[%d] %s ch=%ld rssi=%ld\n", i, foundSsid.c_str(), (long)channel, (long)rssi);
                if (matchedIndex < 0 && foundSsid == ssid) {
                    matchedIndex = i;
                }
            }
        } else {
            Serial.println("掃描 AP 失敗或未找到任何 AP");
        }

        if (matchedIndex >= 0) {
            uint8_t* bssid = WiFi.BSSID(matchedIndex);
            int32_t channel = WiFi.channel(matchedIndex);
            Serial.printf("使用 BSSID 定向連線: ch=%ld\n", (long)channel);
            WiFi.begin(ssid.c_str(), password.c_str(), channel, bssid, true);
        } else {
            Serial.println("未在掃描結果找到目標 SSID，改用一般連線");
            WiFi.begin(ssid.c_str(), password.c_str());
        }
        WiFi.scanDelete();

        _connectStartTime = millis();
        _lastConnectDotAt = _connectStartTime;
        _lastStatusLogAt = _connectStartTime;
        _lastStatus = WL_IDLE_STATUS;
        _connectUsingStoredCredential = false;
        _connectInProgress = true;
        return true;
    }

    // 使用 ESP SDK/NVS 中已儲存的 WiFi 憑證連線（不依賴 /wifi.json）
    bool startConnectStoredWiFi() {
        Serial.println("嘗試使用 SDK 儲存的 WiFi 憑證連線...");

        WiFi.persistent(true);
        WiFi.setAutoReconnect(true);
        WiFi.mode(WIFI_STA);
        delay(30);
        WiFi.disconnect();
        delay(80);
        WiFi.begin();

        _connectStartTime = millis();
        _lastConnectDotAt = _connectStartTime;
        _lastStatusLogAt = _connectStartTime;
        _lastStatus = WL_IDLE_STATUS;
        _connectUsingStoredCredential = true;
        _connectInProgress = true;
        return true;
    }

    ConnectResult pollConnect() {
        if (!_connectInProgress) return CONNECT_IDLE;

        if (WiFi.status() == WL_CONNECTED) {
            // 若由 SDK 成功連線，回填目前 SSID 供 UI/日誌使用
            if (_connectUsingStoredCredential) {
                ssid = WiFi.SSID();
            }
            localIP = WiFi.localIP().toString();
            isAPMode = false;
            _connectInProgress = false;

            if (_connectUsingStoredCredential) {
                Serial.printf("\nSDK WiFi 已連線! SSID=%s IP=%s\n", ssid.c_str(), localIP.c_str());
            } else {
                Serial.printf("\nWiFi 已連線! IP: %s\n", localIP.c_str());
            }
            return CONNECT_SUCCESS;
        }

        unsigned long now = millis();
        wl_status_t status = WiFi.status();

        if (status != _lastStatus || now - _lastStatusLogAt >= 2000) {
            _lastStatus = status;
            _lastStatusLogAt = now;
            Serial.printf(" [WiFi:%s]", wifiStatusToString(status));
        }

        if (now - _connectStartTime > WIFI_CONNECT_TIMEOUT) {
            _connectInProgress = false;
            if (_connectUsingStoredCredential) {
                Serial.printf("\nSDK WiFi 連線超時 (status=%s)\n", wifiStatusToString(status));
            } else {
                Serial.printf("\nWiFi 連線超時 (status=%s)\n", wifiStatusToString(status));
            }
            return CONNECT_TIMEOUT;
        }

        if (now - _lastConnectDotAt >= 500) {
            _lastConnectDotAt = now;
            Serial.print(".");
        }

        return CONNECT_IN_PROGRESS;
    }

    void cancelConnect() {
        _connectInProgress = false;
        WiFi.disconnect();
    }

    bool connectWiFi() {
        if (!startConnectWiFi()) return false;
        while (true) {
            ConnectResult result = pollConnect();
            if (result == CONNECT_SUCCESS) return true;
            if (result == CONNECT_TIMEOUT || result == CONNECT_FAILED) return false;
            delay(10);
            yield();
        }
    }

    bool connectStoredWiFi() {
        if (!startConnectStoredWiFi()) return false;
        while (true) {
            ConnectResult result = pollConnect();
            if (result == CONNECT_SUCCESS) return true;
            if (result == CONNECT_TIMEOUT || result == CONNECT_FAILED) return false;
            delay(10);
            yield();
        }
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
        _connectInProgress = false;

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

private:
    bool _fsReady = false;
    bool _connectInProgress = false;
    bool _connectUsingStoredCredential = false;
    unsigned long _connectStartTime = 0;
    unsigned long _lastConnectDotAt = 0;
    unsigned long _lastStatusLogAt = 0;
    wl_status_t _lastStatus = WL_IDLE_STATUS;

    const char* wifiStatusToString(wl_status_t status) {
        switch (status) {
            case WL_CONNECTED:
                return "CONNECTED";
            case WL_NO_SSID_AVAIL:
                return "NO_SSID";
            case WL_CONNECT_FAILED:
                return "CONNECT_FAILED";
            case WL_CONNECTION_LOST:
                return "CONNECTION_LOST";
            case WL_DISCONNECTED:
                return "DISCONNECTED";
            case WL_SCAN_COMPLETED:
                return "SCAN_COMPLETED";
            case WL_IDLE_STATUS:
            default:
                return "IDLE";
        }
    }
};

#endif
