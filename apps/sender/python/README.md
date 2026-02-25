# Sender v2 (Python)

Cross-platform metrics sender for Ubuntu/Windows.

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate  # Windows: .venv\\Scripts\\activate
pip install -r requirements.txt
```

## Run

```bash
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 python sender_v2.py
```

Optional env vars:
- `SENDER_HOSTNAME`
- `SEND_INTERVAL_SEC` (default `1.0`)
- `MQTT_USER`
- `MQTT_PASS`
- `MQTT_QOS`

Publishes to:
- `sys/agents/<hostname>/metrics/v2`
