# ESP12-Blink Optimization Checklist

Use this checklist when applying the skill to this repository.

## Reliability Hotspots

1. Replace long fixed `delay(...)` retries in connect flows with timed state transitions.
Current hotspots include WiFi and MQTT connection paths in `src/main.cpp`, `src/include/wifi_manager.h`, and `src/include/web_server.h`.

2. Keep async/web request handlers short and non-blocking.
Avoid waiting loops inside HTTP handlers; queue work and return immediately where possible.

3. Audit all `while (...)` loops for watchdog safety.
Ensure each long loop either exits quickly or yields frequently.

## Memory/Flash Hotspots

1. Review heavy `String` allocation paths in frequently called code.
Prioritize message parsing/display update paths where fragmentation risk grows over uptime.

2. Validate large static web payload strategy.
Large inline HTML in `src/include/html_monitor.h` is already stored with `PROGMEM`; keep this approach and avoid runtime concatenation.

3. Check dynamic allocation lifecycle.
Pointers created in `src/main.cpp` (e.g., monitor/web manager instances) should be justified or converted to static lifecycle where safe.

## Security Hotspots

1. Remove or redact credential-bearing logs.
Never print passwords or secrets to serial output, even in setup flows.

2. Harden config endpoints.
If the web UI is reachable on production LAN, add auth/rate-limit/CSRF protections consistent with device constraints.

3. Use secure MQTT transport when threat model requires it.
Plan memory for TLS and validate reconnect behavior under broker cert/auth failures.

## Verification Sequence

Run these after each substantial change:

1. `pio run`
2. `pio test`
3. Soak test: 30+ minutes with periodic MQTT traffic
4. Failure injection: AP off/on, broker off/on, malformed payloads
5. Restart test: power cycle and confirm stable mode recovery
