# WiFi WebUI 實作計畫

## 任務列表

### 1. 更新 platformio.ini - 新增依賴庫
- [ ] 新增 ESP8266WiFi
- [ ] 新增 ESPAsyncWebServer
- [ ] 新增 ArduinoJson
- [ ] 新增 qrcode
- [ ] 新增 LittleFS

### 2. 建立 TFT 文字功能
- [ ] 建立 font_8x16.h 點陣字型
- [ ] 在 tft_driver.h 新增 drawChar() 函式
- [ ] 新增 drawString() 函式
- [ ] 測試文字顯示

### 3. 建立 QR Code 顯示功能
- [ ] 建立 qr_display.h
- [ ] 實作 drawQRCode() 函式
- [ ] 測試 QR Code 顯示

### 4. 建立 WiFi 管理模組
- [ ] 建立 wifi_manager.h
- [ ] 實作 loadConfig() - 從 LittleFS 讀取
- [ ] 實作 saveConfig() - 儲存到 LittleFS
- [ ] 實作 connectWiFi() - 連線 STA
- [ ] 實作 startAP() - 啟動 AP 模式
- [ ] 實作 getDeviceID() - 取得 MAC 後 6 碼

### 5. 建立 WebUI
- [ ] 建立 html_page.h - HTML 模板
- [ ] 建立 web_server.h
- [ ] 實作 GET / - 設定頁面
- [ ] 實作 GET /scan - WiFi 掃描
- [ ] 實作 POST /save - 儲存設定

### 6. 整合主程式
- [ ] 重構 main.cpp
- [ ] 實作啟動流程
- [ ] 實作螢幕顯示邏輯
- [ ] 測試完整流程

## 執行順序

```
1 → 2 → 3 → 4 → 5 → 6
```

每個步驟完成後測試，確保功能正常。
