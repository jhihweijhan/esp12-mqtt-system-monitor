#!/usr/bin/env bash
set -euo pipefail

TARGET_PATH="${1:-src}"

if ! command -v rg >/dev/null 2>&1; then
    echo "ERROR: ripgrep (rg) is required for this audit script." >&2
    exit 1
fi

if [[ ! -e "$TARGET_PATH" ]]; then
    echo "ERROR: target path does not exist: $TARGET_PATH" >&2
    exit 1
fi

echo "ESP8266 IoT Audit"
echo "Target: $TARGET_PATH"
echo

print_section() {
    echo "== $1 =="
}

safe_rg() {
    local pattern="$1"
    rg -n --no-heading -S "$pattern" "$TARGET_PATH" || true
}

safe_rg_code() {
    local pattern="$1"
    rg -n --no-heading -S \
        -g '*.c' -g '*.cc' -g '*.cpp' -g '*.h' -g '*.hpp' -g '*.ino' \
        "$pattern" "$TARGET_PATH" | rg -v '/html_.*\.h:' || true
}

print_section "Potential Blocking Delays (>=100ms)"
safe_rg_code 'delay\(([1-9][0-9]{2,}|[1-9][0-9]{3,})\)'
echo

print_section "Potential Tight Loops"
safe_rg_code 'while\s*\(.*\)\s*\{'
echo

print_section "Watchdog Yield Usage"
safe_rg_code '\byield\s*\(\s*\)|delay\s*\(\s*0\s*\)'
echo

print_section "Dynamic Allocation Usage"
safe_rg_code '\bnew\s+[A-Za-z_][A-Za-z0-9_:<>]*'
echo

print_section "String Heavy Usage Hotspots"
safe_rg_code '\bString\b'
echo

print_section "Credential/Secret Handling Signals"
safe_rg_code 'password|passwd|mqttPass|token|secret|api[_-]?key'
echo

print_section "Serial Logging That May Leak Sensitive Data"
safe_rg_code 'Serial\.(print|println|printf)\s*\(.*(pass|password|token|secret|key)'
echo

print_section "MQTT Transport Clues (TLS vs Plain)"
safe_rg_code 'WiFiClientSecure|BearSSL|setInsecure|setFingerprint|setTrustAnchors|PubSubClient|setServer'
echo

print_section "Filesystem and Persistence Usage"
safe_rg_code 'LittleFS|SPIFFS|EEPROM'
echo

print_section "Build/Test Command Reminder"
echo "Run after fixes:"
echo "  pio run"
echo "  pio test"
