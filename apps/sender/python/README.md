# Sender v2 (Python, uv workflow)

Cross-platform metrics sender for Ubuntu/Windows.

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

If broker is not ready, sender now retries with backoff instead of exiting.

GPU metrics source (auto-detect):
- NVIDIA: `nvidia-smi`
- AMD: `rocm-smi --json`
- If neither tool is available, GPU fields fall back to `0`.

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
