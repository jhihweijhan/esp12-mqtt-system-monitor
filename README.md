# ESP12 Monitor Monorepo

This repo contains both ESP8266 firmware and cross-platform metrics senders.

## Layout

- `apps/firmware`: PlatformIO ESP12 firmware (`/monitor`, MQTT receive/render)
- `apps/sender/python`: Python sender v2 (Ubuntu/Windows, uv workflow)
- `apps/sender/go`: Go sender v2 (single binary path)
- `docs/protocol/metrics-v2.md`: sender/firmware payload contract

## Branch

Current rewrite branch:

- `feat/metrics-v2-rewrite`

Update branch:

```bash
git pull --rebase
```

## Firmware quick start

```bash
cd apps/firmware
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor --baud 115200
```

Unit tests (policy tests are native-only):

```bash
~/.platformio/penv/bin/pio test -e native
```

Notes:

- `upload_port` is configured in `apps/firmware/platformio.ini`.
- `pio test -e esp12e_wifi` is expected to run `0` tests in this branch.

## Sender protocol

Topic:

- `sys/agents/<hostname>/metrics/v2`

Payload shape:

- `v`, `ts`, `h`, `cpu[]`, `ram[]`, `gpu[]`, `net[]`, `disk[]`

See `docs/protocol/metrics-v2.md` for details.

## Python sender (uv)

Consumer-friendly startup (Ubuntu):

```bash
cd apps/sender/python
chmod +x senderctl.sh
./senderctl.sh quickstart-compose
```

Then manage with:

```bash
./senderctl.sh compose-status
./senderctl.sh compose-logs
```

Advanced/manual mode:

```bash
cd apps/sender/python
uv sync --extra dev
uv run python -m pytest -q
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=your_user MQTT_PASS=your_pass uv run python sender_v2.py
```

Windows PowerShell:

```powershell
$env:MQTT_HOST="127.0.0.1"
$env:MQTT_PORT="1883"
$env:MQTT_USER="your_user"
$env:MQTT_PASS="your_pass"
uv run python sender_v2.py
```

## Go sender

```bash
cd apps/sender/go
go mod tidy
go build -o sender_v2 .
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=your_user MQTT_PASS=your_pass ./sender_v2
```

Firmware side:

- Set broker host/port/user/pass in `/monitor` Web UI before expecting MQTT connect success.

## Common issues

- `ConnectionRefusedError: [Errno 111]` from Python sender:
  - MQTT broker is not listening on `MQTT_HOST:MQTT_PORT`.
  - Verify broker is up, or set correct env vars.
  - Sender now retries with backoff instead of exiting.
- `collect2: error: ld returned 1 exit status` while testing:
  - Do not run policy unit test under `esp12e_wifi`.
  - Use `~/.platformio/penv/bin/pio test -e native`.
