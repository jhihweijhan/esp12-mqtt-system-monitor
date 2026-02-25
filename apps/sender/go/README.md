# Sender v2 (Go)

Single-binary sender for Ubuntu/Windows.

## Build

```bash
go mod tidy
go build -o sender_v2 .
```

## Run

```bash
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 ./sender_v2
```

Optional env vars:
- `SENDER_HOSTNAME`
- `SEND_INTERVAL_SEC` (default `1.0`)
- `MQTT_USER`
- `MQTT_PASS`
- `MQTT_QOS`

Publishes to:
- `sys/agents/<hostname>/metrics/v2`
