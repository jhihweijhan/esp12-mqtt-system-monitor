# Sender v2 (Python, uv workflow)

Cross-platform metrics sender for Ubuntu/Windows.

## Consumer startup (Ubuntu, recommended)

One command setup + Docker Compose daemon mode:

```bash
cd apps/sender/python
chmod +x senderctl.sh
./senderctl.sh quickstart-compose
```

This opens a setup wizard once, saves config to `~/.config/esp12-sender/sender.env`,
syncs `.docker.env`, then runs `docker compose up -d --build`.
Container restart policy is `unless-stopped`, so it restarts automatically after reboot.

Common management commands:

```bash
./senderctl.sh compose-status
./senderctl.sh compose-logs
./senderctl.sh compose-restart
```

If Docker is not enabled at boot yet:

```bash
./senderctl.sh ensure-docker-boot
```

Legacy native mode is still available (`quickstart`, `install-service`).

## Setup (uv)

```bash
uv sync
```

Install dev tooling (pytest):

```bash
uv sync --extra dev
```

## Run

```bash
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=your_user MQTT_PASS=your_pass uv run python sender_v2.py
```

Or run with saved config:

```bash
./senderctl.sh run
```

If broker is not ready, sender now retries with backoff instead of exiting.

GPU metrics source (auto-detect):
- NVIDIA: `nvidia-smi`
- AMD: `rocm-smi --json`
- Linux native fallback: `/sys/class/drm/card*/device/*` (`gpu_busy_percent`, `mem_busy_percent`, `hwmon temp*_input`)
- If all sources are unavailable, GPU fields fall back to `0`.

## Test

```bash
uv run python -m pytest -q
```

Optional env vars:
- `SENDER_HOSTNAME`
- `SEND_INTERVAL_SEC` (default `1.0`)
- `MQTT_USER`
- `MQTT_PASS`
- `MQTT_QOS` (0-2)

If your broker requires authentication, `MQTT_USER` and `MQTT_PASS` are required.

Publishes to:
- `sys/agents/<hostname>/metrics/v2`
