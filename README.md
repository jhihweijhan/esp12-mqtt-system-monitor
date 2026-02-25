# ESP12 Monitor Monorepo

This repo contains both ESP8266 firmware and cross-platform metrics senders.

## Layout

- `apps/firmware`: PlatformIO ESP12 firmware (`/monitor`, MQTT receive/render)
- `apps/sender/python`: Python sender v2 (Ubuntu/Windows)
- `apps/sender/go`: Go sender v2 (single binary path)
- `docs/protocol/metrics-v2.md`: sender/firmware payload contract

## Firmware quick start

```bash
cd apps/firmware
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio test
```

## Sender protocol

Topic:
- `sys/agents/<hostname>/metrics/v2`

Payload shape:
- `v`, `ts`, `h`, `cpu[]`, `ram[]`, `gpu[]`, `net[]`, `disk[]`

See `docs/protocol/metrics-v2.md` for details.

## Python sender

```bash
cd apps/sender/python
python3 -m venv .venv
source .venv/bin/activate  # Windows: .venv\\Scripts\\activate
pip install -r requirements.txt
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 python sender_v2.py
```

## Go sender

```bash
cd apps/sender/go
go mod tidy
go build -o sender_v2 .
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 ./sender_v2
```
