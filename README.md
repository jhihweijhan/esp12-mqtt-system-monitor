# ESP12 System Monitor V2.0

ESP12F WiFi + MQTT 系統監控器，可顯示多台設備的 CPU/RAM/GPU/網路/磁碟狀態。

## 功能

- 📶 WiFi 設定 WebUI（AP 模式 QR Code 快速連線）
- 📊 MQTT 訂閱系統監控數據
- 🖥️ 240x240 TFT 顯示器
- 🔄 多設備自動輪播
- ⚠️ 變色警示（綠→黃→紅）
- 🔴 離線設備閃爍警告
- ⚙️ WebUI 設定 MQTT/設備/閾值
- 💾 設定自動儲存（LittleFS）

## 顯示畫面

```
┌──────────────────────────┐
│      desk      1/3       │  ← 設備別名 + 輪播指示
├──────────────────────────┤
│ CPU   87%    62C         │  ← 綠/黃/紅 變色
│ RAM   45%    12.3/32G    │
│ GPU   76%    58C         │
│ VRAM: 45%                │
│ NET  v12.3M  ^3.2M       │
│ DISK R:5.1M  W:2.3M      │
├──────────────────────────┤
│ MQTT OK           3s ago │  ← 狀態列
└──────────────────────────┘
```

## 使用方式

### 首次設定

1. 開機後自動進入 AP 模式
2. 螢幕顯示 QR Code，手機掃描連接 `ESP12-XXXXXX` 網路
3. 開啟瀏覽器輸入 `192.168.4.1`
4. 選擇 WiFi、輸入密碼、儲存
5. 連線成功後顯示 IP 和 WebUI QR Code

### 設定 MQTT

1. 瀏覽 `http://<IP>/monitor`
2. 填入 MQTT 伺服器資訊
3. 設定警示閾值（可選）
4. 儲存後自動重啟

### MQTT Topic 格式

ESP12 訂閱 `hwmonitor/+/metrics`，期望 JSON 格式：

```json
{
  "cpu": {"percent": 87.5, "temp": 62},
  "ram": {"percent": 45, "used_gb": 12.3, "total_gb": 32},
  "gpu": {"percent": 76, "temp": 58, "mem_percent": 45},
  "net": {"rx_mbps": 12.3, "tx_mbps": 3.2},
  "disk": {"read_mbs": 5.1, "write_mbs": 2.3}
}
```

## WebUI 路徑

| 路徑 | 說明 |
|------|------|
| `/` | WiFi 設定頁面 |
| `/monitor` | 監控設定頁面 |
| `/api/config` | 設定 API（GET/POST） |
| `/api/status` | 即時狀態 API |

## 硬體配置

| 功能 | GPIO |
|------|------|
| CS | GPIO16 |
| DC | GPIO0 |
| RST | GPIO4 |
| BL | GPIO5 (LOW=開) |
| MOSI | GPIO13 |
| SCLK | GPIO14 |

## 編譯與上傳

```bash
pio run -t upload
```

## 檔案結構

```
src/
├── main.cpp
└── include/
    ├── tft_driver.h        # TFT 驅動
    ├── font_8x16.h         # 點陣字型
    ├── qr_display.h        # QR Code 繪製
    ├── wifi_manager.h      # WiFi 管理
    ├── web_server.h        # Web 伺服器
    ├── html_page.h         # WiFi 設定頁
    ├── html_monitor.h      # 監控設定頁
    ├── mqtt_client.h       # MQTT 客戶端
    ├── monitor_config.h    # 監控設定管理
    ├── monitor_display.h   # 監控顯示邏輯
    └── ui_components.h     # UI 元件庫
```

## 依賴套件

- ArduinoJson ^7.0.0
- QRCode ^0.0.1
- ESPAsyncWebServer
- PubSubClient ^2.8
