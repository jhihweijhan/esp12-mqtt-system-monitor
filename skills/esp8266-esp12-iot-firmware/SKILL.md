---
name: esp8266-esp12-iot-firmware
description: Use when an agent needs to design, harden, debug, or optimize ESP8266/ESP-12 firmware for production IoT use, especially with PlatformIO, WiFi/MQTT, LittleFS, OTA updates, watchdog-safe loops, constrained memory, and security baselines.
---

# Esp8266 Esp12 Iot Firmware

## Overview

Use this skill to make ESP8266/ESP-12 firmware safer, more stable, and easier to ship under constrained CPU/RAM/flash and real IoT network conditions.

Violating resource limits or security controls on ESP8266 usually appears as random resets, connect loops, heap fragmentation, or unsafe remote access. This skill provides an order of operations that prevents those failures.

## Workflow

1. Triage the request into one of three modes: `new feature`, `hardening`, or `performance optimization`.
2. Run `scripts/esp8266_iot_audit.sh` on the target path to collect high-risk patterns before touching code.
3. Apply the Resource Safety Baseline (boot pins, WDT-safe loops, heap/flash limits, sleep behavior).
4. Apply the IoT Security Baseline (MQTT transport/auth, OTA integrity, secret handling, access control).
5. Apply the Optimization Playbook (non-blocking flow, JSON footprint, network backoff, display/update cadence).
6. Verify with build + tests + runtime logs (including reconnect, offline/online transitions, memory trend).
7. Return a change summary with before/after behavior and residual risk.

## Quick Triage

| Symptom | First Check | Typical Fix |
|---|---|---|
| Random reboot / WDT reset | Long blocking loops and missing `yield()` | Convert to state-machine style loops and yield every short cycle |
| Unstable boot after hardware change | ESP8266 boot strap pins wiring | Recheck GPIO0/GPIO2/GPIO15 default levels and 3.3V-only IO |
| MQTT reconnect storms | Retry logic and keepalive loop cadence | Add exponential backoff, jitter, and robust `loop()` cadence |
| Heap shrinking over time | `String` churn and large temporary buffers | Pre-allocate buffers, reduce dynamic concatenation, move constants to flash |
| OTA failures or takeover risk | OTA auth/signing and update path | Require authenticated OTA and signed binaries for production |
| Secret leakage in logs/repo | Plaintext credentials in files/serial | Remove hardcoded secrets, redact logs, use environment config |

## Resource Safety Baseline

1. Enforce boot-safe hardware assumptions.
Check ESP8266 strap pin behavior and avoid 5V IO assumptions before firmware debugging.

2. Enforce watchdog-safe cooperative scheduling.
Avoid long `delay()` blocks in high-frequency paths. Keep loops short, call `yield()`/`delay(0)` where needed, and prefer non-blocking timers/state transitions.

3. Enforce memory discipline.
Track free heap and fragmentation during long runs. Limit dynamic `String` churn, minimize temporary JSON/document buffers, and keep immutable literals in flash where practical.

4. Enforce flash/filesystem fit.
Verify flash map and FS image sizes before release, and avoid runtime writes that can wear flash or fail under low space.

5. Enforce deep-sleep/wake correctness.
Handle RF state and wake transitions explicitly to prevent immediate reconnect failures or unstable post-wake behavior.

## IoT Security Baseline

Map controls to recognized IoT guidance and protocol specs:

1. Device identity and configuration integrity.
Use stable device identifiers and controlled config update paths.

2. Protected data transport and interface access.
Prefer TLS for MQTT links and gate critical interfaces with auth and least privilege.

3. Verified software updates.
Use authenticated OTA in development and signed update pipelines in production.

4. Secure defaults and recoverability.
Ship with safe defaults, bounded retries, and observable security state (last update, auth status, connectivity status).

5. Secret hygiene.
Never hardcode WiFi/MQTT credentials in firmware source. Keep secrets outside version control and redact serial/web responses.

## Optimization Playbook

1. Convert blocking flows to event-driven state updates.
WiFi connect, MQTT reconnect, and sensor/display update loops should share a short non-blocking tick.

2. Limit payload parsing and copy cost.
Parse only required JSON fields and cap message sizes explicitly.

3. Bound retry behavior.
Use backoff + jitter and clear max-attempt policies to prevent broker/AP storms.

4. De-risk long-lived UI/HTML storage.
Keep large static content in flash/FS and reduce repeated in-memory assembly.

5. Verify by behavior, not by compile success.
Test power-cycle, WiFi outage, broker outage, and noisy payload scenarios.

## Required Outputs

When using this skill, always provide:

1. `Risk findings` from audit + manual review.
2. `Patch plan` grouped by resource safety, security, and performance.
3. `Verification evidence` with exact commands and observed outcomes.
4. `Follow-up tasks` for anything not fixed in this session.

## References

- `references/official-best-practices.md`: curated official guidance and standards links.
- `references/esp12-blink-optimization-checklist.md`: targeted checklist for this repository layout.
- `scripts/esp8266_iot_audit.sh`: fast static checks for common reliability/security/perf issues.

## Common Mistakes

- Treating occasional reboot as acceptable instead of root-causing WDT/memory issues.
- Solving reconnect failures with longer blocking delays.
- Enabling OTA without robust authentication/integrity controls.
- Adding features without first measuring heap/fragmentation trend under load.
- Logging credentials or sensitive config during troubleshooting.
