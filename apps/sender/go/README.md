# Sender v2 (Go)

Single-binary sender for Ubuntu/Windows.

## Build

```bash
go mod tidy
go build -o sender_v2 .
```

## Run

```bash
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=your_user MQTT_PASS=your_pass ./sender_v2
```

Optional env vars:
- `SENDER_HOSTNAME`
- `SEND_INTERVAL_SEC` (default `1.0`)
- `MQTT_USER`
- `MQTT_PASS`
- `MQTT_QOS`

If your broker requires authentication, `MQTT_USER` and `MQTT_PASS` are required.

Publishes to:
- `sys/agents/<hostname>/metrics/v2`

GPU metrics source (auto-detect):
- NVIDIA: `nvidia-smi`
- AMD: `rocm-smi --json`
- Linux native fallback: `/sys/class/drm/card*/device/*` (`gpu_busy_percent`, `mem_busy_percent`, `hwmon temp*_input`)
- If all sources are unavailable, GPU fields fall back to `0`.
