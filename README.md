# ESP12-Blink

這是一個以 **ESP8266/ESP12** 為核心的系統監控顯示專案。  
主要用途是把電腦端的 CPU / RAM / GPU / 網路 / 磁碟資訊，透過 MQTT 傳給 ESP12，並在小螢幕上即時顯示。

## 這個倉庫放什麼

- 韌體（ESP12 顯示端）：`apps/firmware`
- Sender（電腦端資料發送）：
  - Python 版：`apps/sender/python`
  - Go 版：`apps/sender/go`
- 通訊協議文件：`docs/protocol/metrics-v2.md`

## 快速開始

### 1) 韌體

```bash
cd apps/firmware
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor --baud 115200
```

### 2) Python Sender（推薦）

```bash
cd apps/sender/python
chmod +x senderctl.sh
./senderctl.sh quickstart-compose
```

常用管理：

```bash
./senderctl.sh compose-status
./senderctl.sh compose-logs
```

### 3) Go Sender

```bash
cd apps/sender/go
go test ./...
go build -o sender_v2 .
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=your_user MQTT_PASS=your_pass ./sender_v2
```

## MQTT Topic

- `sys/agents/<hostname>/metrics/v2`

Payload 規格請看：`docs/protocol/metrics-v2.md`
