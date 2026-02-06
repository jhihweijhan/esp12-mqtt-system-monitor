# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build
pio run

# Build and upload
pio run -t upload

# Upload filesystem (LittleFS)
pio run -t uploadfs

# Serial monitor
pio device monitor --baud 115200
```

## Architecture

ESP12F-based system monitor with 240x240 TFT display, receiving metrics via MQTT and configurable through WebUI.

### Core Components

```
main.cpp                    Entry point, mode switching (AP_SETUP / MONITOR)
    ↓
├── TFTDriver              Low-level SPI display driver (GPIO: CS=16, DC=0, RST=4, BL=5)
├── WiFiManager            WiFi connection + AP mode management, config in LittleFS
├── MQTTClient             PubSubClient wrapper, subscribes to sys/agents/+/metrics
├── MonitorConfigManager   JSON config persistence (devices, thresholds, MQTT settings)
├── MonitorDisplay         Display rendering with carousel logic
└── WebServerManager       ESPAsyncWebServer routes (/api/config, /api/status)
```

### Data Flow

1. `MQTTClient` receives JSON from `sys/agents/{hostname}/metrics`
2. Parses into `DeviceMetrics` struct (CPU, RAM, GPU, network, disk)
3. `MonitorDisplay::loop()` renders current device every 500ms
4. Carousel switches devices based on `displayTime` setting
5. Only `enabled` devices participate in carousel

### Key Patterns

- **WDT Safety**: Call `yield()` after heavy operations (JSON parsing, display updates)
- **Delayed Save**: `MonitorConfigManager::loop()` handles deferred LittleFS writes to avoid WDT in callbacks
- **Flicker Prevention**: Only redraw header/static elements on device switch (`_forceRedraw` flag)
- **Large MQTT Messages**: Buffer set to 16KB for ~8KB JSON payloads

### WebUI Routes

| Route | Method | Purpose |
|-------|--------|---------|
| `/` | GET | WiFi setup (AP mode) or redirect to /monitor |
| `/monitor` | GET | Dashboard settings page |
| `/api/config` | GET/POST | Read/write monitor configuration |
| `/api/status` | GET | Real-time device status |

### Hardware Pinout (ESP12F + ST7789 240x240)

| Signal | GPIO | Note |
|--------|------|------|
| CS | 16 | Chip select |
| DC | 0 | Data/Command |
| RST | 4 | Reset |
| BL | 5 | Backlight (LOW=ON) |
| MOSI | 13 | SPI data |
| SCLK | 14 | SPI clock |

## MQTT JSON Format (agent_sender_async.py)

```json
{
  "cpu": {"percent_total": 45.0},
  "memory": {"ram": {"percent": 65.0, "used": bytes, "total": bytes}},
  "gpu": {"usage_percent": 50.0, "temperature_celsius": 65.0, "memory_percent": 30.0},
  "network_io": {"total": {"rate": {"rx_bytes_per_s": 12345, "tx_bytes_per_s": 6789}}},
  "disk_io": {"nvme0n1": {"rate": {"read_bytes_per_s": 1234, "write_bytes_per_s": 5678}}},
  "temperatures": {"coretemp": [{"current": 55.0}]}
}
```
