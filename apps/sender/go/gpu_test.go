package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestParseRocmTelemetryHandlesVariantKeys(t *testing.T) {
	raw := `{"card0":{"Temp Edge (C)":"41.0","Temp Junction (C)":"56.0","Temp Memory (C)":"66.0","GPU Busy (%)":"19","VRAM use (%)":"22"}}`
	got, ok := parseRocmTelemetry(raw)
	if !ok {
		t.Fatalf("expected rocm telemetry to parse")
	}
	if got.usage != 19 || got.temp != 41 || got.memPct != 22 || got.hotspot != 56 || got.memTemp != 66 {
		t.Fatalf("unexpected telemetry parsed: %+v", got)
	}
}

func TestReadGPUTelemetryFromSysfs(t *testing.T) {
	root := t.TempDir()
	hwmon := filepath.Join(root, "card0", "device", "hwmon", "hwmon9")
	if err := os.MkdirAll(hwmon, 0o755); err != nil {
		t.Fatalf("mkdir failed: %v", err)
	}

	mustWrite := func(path, body string) {
		if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
			t.Fatalf("write %s failed: %v", path, err)
		}
	}

	mustWrite(filepath.Join(root, "card0", "device", "gpu_busy_percent"), "37\n")
	mustWrite(filepath.Join(root, "card0", "device", "mem_busy_percent"), "13\n")
	mustWrite(filepath.Join(hwmon, "temp1_label"), "edge\n")
	mustWrite(filepath.Join(hwmon, "temp1_input"), "47000\n")
	mustWrite(filepath.Join(hwmon, "temp2_label"), "junction\n")
	mustWrite(filepath.Join(hwmon, "temp2_input"), "61000\n")
	mustWrite(filepath.Join(hwmon, "temp3_label"), "mem\n")
	mustWrite(filepath.Join(hwmon, "temp3_input"), "65000\n")

	got, ok := readGPUTelemetryFromSysfs(root)
	if !ok {
		t.Fatalf("expected sysfs telemetry to parse")
	}
	if got.usage != 37 || got.temp != 47 || got.memPct != 13 || got.hotspot != 61 || got.memTemp != 65 {
		t.Fatalf("unexpected telemetry parsed: %+v", got)
	}
}

func TestReadGPUTelemetryFromSysfsPrefersMoreActiveCard(t *testing.T) {
	root := t.TempDir()

	card0Hwmon := filepath.Join(root, "card0", "device", "hwmon", "hwmon1")
	if err := os.MkdirAll(card0Hwmon, 0o755); err != nil {
		t.Fatalf("mkdir card0 failed: %v", err)
	}
	card1Hwmon := filepath.Join(root, "card1", "device", "hwmon", "hwmon2")
	if err := os.MkdirAll(card1Hwmon, 0o755); err != nil {
		t.Fatalf("mkdir card1 failed: %v", err)
	}

	mustWrite := func(path, body string) {
		if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
			t.Fatalf("write %s failed: %v", path, err)
		}
	}

	mustWrite(filepath.Join(root, "card0", "device", "gpu_busy_percent"), "0\n")
	mustWrite(filepath.Join(card0Hwmon, "temp1_label"), "edge\n")
	mustWrite(filepath.Join(card0Hwmon, "temp1_input"), "37000\n")

	mustWrite(filepath.Join(root, "card1", "device", "gpu_busy_percent"), "57\n")
	mustWrite(filepath.Join(card1Hwmon, "temp1_label"), "edge\n")
	mustWrite(filepath.Join(card1Hwmon, "temp1_input"), "43000\n")

	got, ok := readGPUTelemetryFromSysfs(root)
	if !ok {
		t.Fatalf("expected sysfs telemetry to parse")
	}
	if got.usage != 57 || got.temp != 43 {
		t.Fatalf("unexpected telemetry parsed: %+v", got)
	}
}
