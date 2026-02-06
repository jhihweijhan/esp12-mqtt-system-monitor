# WiFi WebUI 設計文件

## 概述

為「暗大浪事 V1.9」ESP12F 板新增 WiFi 設定功能，透過 WebUI 輸入 WiFi 帳密，並在 TFT 螢幕顯示連線狀態與 QR Code。

## 系統流程

```
啟動
  ↓
從 LittleFS 讀取 /wifi.json
  ↓
有設定？─────否────→ 進入 AP 模式
  │                    ↓
 是                  螢幕顯示:
  ↓                  - SSID: "ESP12-XXXXXX"
嘗試連線              - WiFi QR Code
  ↓                  - "192.168.4.1"
10秒內成功？──否──→     ↓
  │                  啟動 WebServer
 是                  等待用戶設定
  ↓                     ↓
螢幕顯示:            用戶輸入帳密
- 已連線 SSID           ↓
- IP 地址            儲存到 LittleFS
- WebUI QR Code         ↓
                     重啟設備
```

## 技術規格

### AP 模式

| 項目 | 設定 |
|------|------|
| SSID | ESP12-XXXXXX (MAC 後 6 碼) |
| 密碼 | 無 (開放網路) |
| IP | 192.168.4.1 |
| 觸發條件 | 無設定 或 連線失敗超過 10 秒 |

### 資料儲存

**檔案:** `/wifi.json` (LittleFS)

```json
{
  "ssid": "MyWiFi",
  "pass": "password123"
}
```

### QR Code

| 項目 | 規格 |
|------|------|
| 庫 | qrcode (ricmoo) |
| Version | 3 (29x29 模組) |
| 模組尺寸 | 6 像素 |
| 總尺寸 | 174x174 像素 |
| 置中於 | 240x240 螢幕 |

**QR Code 內容:**
- AP 模式: `WIFI:T:nopass;S:ESP12-XXXXXX;;`
- STA 模式: `http://192.168.x.x`

### WebUI

**端點:**
| 路徑 | 方法 | 功能 |
|------|------|------|
| `/` | GET | 設定頁面 |
| `/scan` | GET | 掃描 WiFi 列表 (JSON) |
| `/save` | POST | 儲存設定並連線 |

**頁面功能:**
- WiFi 下拉選單 (自動掃描)
- 密碼輸入 (可切換顯示)
- 儲存按鈕
- 連線狀態提示

**技術:**
- ESPAsyncWebServer
- 純 HTML + 內嵌 CSS
- 約 2KB 程式碼內嵌

### 文字顯示

| 項目 | 規格 |
|------|------|
| 字型 | 8x16 點陣 ASCII |
| 大小 | ~4KB |
| 字元 | 0-9 A-Z a-z . : / - |

### 螢幕佈局

**AP 模式:**
```
┌──────────────────────┐
│    設定 WiFi          │ 16px
│   ESP12-A3B4C5       │ 16px
│                      │
│   ┌──────────────┐   │
│   │   QR Code    │   │ 174x174
│   │  (連線 AP)   │   │
│   └──────────────┘   │
│                      │
│   192.168.4.1        │ 16px
└──────────────────────┘
```

**STA 模式 (已連線):**
```
┌──────────────────────┐
│    已連線 WiFi        │ 16px
│    MyNetwork         │ 16px
│                      │
│   ┌──────────────┐   │
│   │   QR Code    │   │ 174x174
│   │ (開啟 WebUI) │   │
│   └──────────────┘   │
│                      │
│   192.168.1.100      │ 16px
└──────────────────────┘
```

## 依賴庫

```ini
lib_deps =
    ESP8266WiFi
    ESPAsyncWebServer
    ArduinoJson
    qrcode
```

## 檔案結構

```
src/
├── main.cpp          # 主程式
├── wifi_manager.h    # WiFi 管理
├── web_server.h      # WebUI 伺服器
├── qr_display.h      # QR Code 繪製
├── tft_driver.h      # TFT 驅動 (現有)
├── font_8x16.h       # 點陣字型
└── html_page.h       # WebUI HTML
```
