# Minimal Resource Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor firmware runtime paths to a minimal viable architecture with lower RAM/CPU pressure and better long-run MQTT/WiFi stability on ESP12.

**Architecture:** Keep external behavior and WebUI endpoints intact, but replace heavy runtime internals with bounded-memory and low-fragmentation implementations. Prioritize zero-heap churn in hot paths, bounded reconnect behavior, and reduced display refresh pressure.

**Tech Stack:** ESP8266 Arduino core, PubSubClient, ArduinoJson, ESPAsyncWebServer, LittleFS, PlatformIO

### Task 1: Stabilize Runtime Object Lifetime

**Files:**
- Modify: `src/main.cpp`

**Step 1: Refactor global object ownership**
- Replace `new`-allocated global pointers for `WebServerManager` and `MonitorDisplay` with static/global instances.
- Add lightweight boolean flags for one-time `begin()` initialization.

**Step 2: Refactor mode transitions**
- Keep `startAPMode()` and `startMonitorMode()` flow, but remove dynamic allocation branches.
- Ensure callbacks and dependencies are wired once.

**Step 3: Verify build**
- Run `pio run`
- Expected: successful build.

### Task 2: Reduce MQTT Hot-Path Resource Usage

**Files:**
- Modify: `include/connection_policy.h`
- Modify: `src/include/mqtt_client.h`

**Step 1: Bound network waiting and retries**
- Keep reconnect backoff, plus short socket timeout and throttled WiFi reconnect attempts.

**Step 2: Use bounded parser state**
- Replace hot-path dynamic JSON document usage with reusable static-capacity JSON docs in `MQTTClient`.
- Initialize and reuse ArduinoJson filter document to parse only required fields.

**Step 3: Minimize message parsing work**
- Parse only metrics used by current display flow.
- Keep offline/heartbeat behavior unchanged.

**Step 4: Verify unit policy tests**
- Run `pio test -e native`
- Expected: all tests pass.

### Task 3: Reduce Display Load

**Files:**
- Modify: `include/connection_policy.h`
- Modify: `src/include/monitor_display.h`

**Step 1: Slow non-critical refresh cadence**
- Increase active/force redraw intervals to reduce SPI and CPU pressure.

**Step 2: Keep zero-allocation rendering paths**
- Preserve fixed-buffer rendering for frequently updated bottom status fields.

**Step 3: Verify build**
- Run `pio run`
- Expected: successful build with no new warnings from project files.

### Task 4: Regression Guardrails

**Files:**
- Modify: `test/test_connection_policy/test_main.cpp`

**Step 1: Add/adjust tests for new policy limits**
- Assert expected bounds for socket timeout and reconnect intervals.
- Assert display refresh policy constants via policy helper outputs.

**Step 2: Verify tests**
- Run `pio test -e native`
- Expected: all tests pass.

### Task 5: Final Verification

**Step 1: Run complete checks**
- `pio test -e native`
- `pio run`

**Step 2: Capture runtime footprint from build**
- Record RAM and Flash percentages from PlatformIO output for comparison.
