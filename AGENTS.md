# Repository Guidelines

## Project Structure & Module Organization
This is a PlatformIO firmware project for an ESP12F-based MQTT system monitor.
- `src/main.cpp`: application entry point and mode flow (`AP_SETUP` vs `MONITOR`).
- `src/include/*.h`: core modules (display driver, WiFi manager, web server, MQTT client, config, UI/html).
- `docs/`: design notes and implementation plans.
- `test/`: PlatformIO unit/integration tests.
- `include/` and `lib/`: optional shared headers/libraries (keep reusable code here if added).
- `.pio/`: generated build artifacts and downloaded dependencies (do not edit).

## Build, Test, and Development Commands
Run commands from repo root:
- `pio run`: compile firmware for the default env (`esp12e_wifi`).
- `pio run -t upload`: build and flash via configured serial port (`/dev/ttyUSB0`).
- `pio run -t uploadfs`: upload LittleFS filesystem assets.
- `pio device monitor --baud 115200`: open serial monitor.
- `pio test`: run tests under `test/` (add tests as features are added).

## Coding Style & Naming Conventions
- Language: Arduino C++ (ESP8266 framework), 4-space indentation, braces on same line.
- Types/classes: `PascalCase` (e.g., `MonitorConfigManager`).
- Functions/variables: `camelCase` (e.g., `showMQTTConnectingScreen`).
- Constants/macros: `UPPER_SNAKE_CASE` (e.g., `COLOR_BLACK`).
- Keep headers focused by domain (`wifi_manager.h`, `monitor_display.h`) and avoid monolithic files.
- Prefer non-blocking loops and `yield()` in heavy paths to avoid WDT resets.

## Testing Guidelines
- Use PlatformIO Test Runner (`pio test`) with tests placed in `test/`.
- Name test files by feature, e.g., `test_monitor_config.cpp`, `test_mqtt_parser.cpp`.
- Focus coverage on JSON parsing, config persistence (LittleFS), and mode transitions/restart behavior.
- For hardware-dependent changes, include manual verification notes (serial output + expected display/WebUI behavior).

## Commit & Pull Request Guidelines
- Follow Conventional Commits seen in history: `feat:`, `fix:`, `perf:` (imperative, concise subject).
- Keep each commit scoped to one behavior change.
- PRs should include a clear behavior summary, linked issue/task (if any), verification evidence (`pio run`, `pio test`, and device/manual checks), and screenshots or serial logs for UI/display/network changes.

## Security & Configuration Tips
- Never commit WiFi/MQTT secrets or device-specific credentials.
- Treat `platformio.ini` upload port and broker settings as environment-specific; document local overrides in PRs.

## Landing the Plane (Session Completion)

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd sync
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
Use 'bd' for task tracking
