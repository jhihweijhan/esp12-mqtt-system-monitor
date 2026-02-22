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
3. Topic Pattern 可保留預設（僅相容舊設定）
4. 在 `Topics` 分頁勾選要訂閱的 sender topics（`sys/agents/<hostname>/metrics`）
5. 可在同一列自訂 Alias（顯示名稱）
6. 設定警示閾值（可選）
7. 儲存後自動重啟

### 搭配 Sender 專案（Docker）

若要使用對應的 sender 專案（`/home/jwj/Workspace/Toys/hwmonitor-mqtt`），建議使用 ESP 專用容器：

```bash
cd /home/jwj/Workspace/Toys/hwmonitor-mqtt
docker compose --profile esp-agent up -d --build
docker compose logs -f sys_agent_esp
```

`sys_agent_esp` 會自動使用 `SENDER_PROFILE=esp`，降低 payload 大小與發送頻率，較適合 ESP 接收端。

### MQTT Topic 格式

ESP12 只會訂閱 WebUI 勾選的主題（allowlist），格式必須是：

- `sys/agents/<hostname>/metrics`

若未勾選任何主題，ESP 不會訂閱任何 sender topic。

期望 JSON 格式：

```json
{
  "host": "desktop-01",
  "cpu": {"percent_total": 87.5},
  "memory": {"ram": {"percent": 45, "used": 13207024435, "total": 34359738368}},
  "gpu": {"usage_percent": 76, "temperature_celsius": 58, "memory_percent": 45},
  "network_io": {"total": {"rate": {"rx_bytes_per_s": 1612186, "tx_bytes_per_s": 419430}}},
  "disk_io": {"total": {"rate": {"read_bytes_per_s": 5347738, "write_bytes_per_s": 2411724}}}
}
```

## WebUI 路徑

| 路徑 | 說明 |
|------|------|
| `/` | WiFi 設定頁面 |
| `/monitor` | 監控設定頁面 |
| `/api/config` | 設定 API（GET/POST） |
| `/api/status` | 即時狀態 API |

`/api/config` 的 MQTT 欄位重點：

- `mqtt.subscribedTopics`: 使用者勾選後要實際訂閱的主題清單
- `mqtt.availableTopics`: WebUI 可勾選的候選主題（由已知 hostname 產生）

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
