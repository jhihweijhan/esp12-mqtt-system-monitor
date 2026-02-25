# Repository Guidelines

## Project Structure & Module Organization
- `apps/firmware`: ESP8266 PlatformIO firmware (`src/`, `src/include/`, `test/`, `platformio.ini`).
- `apps/sender/python`: Python sender v2 (`sender_v2.py`, `metrics_payload.py`, `tests/`).
- `apps/sender/go`: Go sender v2 (`main.go`, `payload.go`, `payload_test.go`).
- `docs/protocol/metrics-v2.md`: source of truth for MQTT topic/payload contract.
- `docs/plans/`: implementation/design notes; `scripts/`: repo utilities (for example PubSubClient patching).

## Build, Test, and Development Commands
Run from the relevant app directory:

```bash
# Firmware
cd apps/firmware
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio test -e native

# Python sender
cd apps/sender/python
uv sync --extra dev
uv run python -m pytest -q
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=... MQTT_PASS=... uv run python sender_v2.py

# Go sender
cd apps/sender/go
go test ./...
go build -o sender_v2 .
```

## Coding Style & Naming Conventions
- Firmware (`.cpp/.h`): 4-space indentation, `CamelCase` types, `lowerCamelCase` functions, `UPPER_SNAKE_CASE` constants/macros.
- Python: follow PEP 8, use type hints and dataclasses where practical, keep modules/functions in `snake_case`.
- Go: always format with `gofmt`; use idiomatic naming (`CamelCase` exported, `lowerCamel` internal).
- Keep protocol fields/topic paths aligned with `docs/protocol/metrics-v2.md`.

## Testing Guidelines
- Firmware policy tests run in native env only: `~/.platformio/penv/bin/pio test -e native`.
- Python tests live in `apps/sender/python/tests` and follow `test_*.py`.
- Go tests live beside code and follow `*_test.go`.
- Add or update tests with every behavior change, especially for topic parsing and payload shape.

## Commit & Pull Request Guidelines
- Follow observed Conventional Commit style: `feat: ...`, `fix: ...`, `docs: ...`, `chore: ...`.
- Prefer short, imperative commit summaries (optionally scoped), one logical change per commit.
- PRs should include: problem statement, affected app(s), exact test commands run, and linked issue/task.
- Include screenshots for `/monitor` UI changes and update protocol docs when payload/topic contracts change.

## Security & Configuration Tips
- Never commit MQTT credentials or host-specific secrets.
- Use environment variables for sender credentials; configure firmware broker settings through `/monitor`.
- Verify `upload_port` in `apps/firmware/platformio.ini` before flashing hardware.
