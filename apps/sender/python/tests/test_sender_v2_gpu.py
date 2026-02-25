from __future__ import annotations

import sender_v2


def test_get_gpu_snapshot_uses_nvidia(monkeypatch):
    monkeypatch.setattr(sender_v2, "_NVIDIA_SMI", "/usr/bin/nvidia-smi")
    monkeypatch.setattr(sender_v2, "_ROCM_SMI", "/usr/bin/rocm-smi")

    def fake_check_output(cmd, **kwargs):
        assert cmd[0] == "/usr/bin/nvidia-smi"
        return "42, 61, 8192, 16384\n"

    monkeypatch.setattr(sender_v2.subprocess, "check_output", fake_check_output)

    snapshot = sender_v2.get_gpu_snapshot()
    assert snapshot.percent == 42.0
    assert snapshot.temp_c == 61.0
    assert snapshot.mem_percent == 50.0


def test_get_gpu_snapshot_falls_back_to_rocm(monkeypatch):
    monkeypatch.setattr(sender_v2, "_NVIDIA_SMI", None)
    monkeypatch.setattr(sender_v2, "_ROCM_SMI", "/usr/bin/rocm-smi")

    rocm_json = (
        '{"card0":{"Temperature (Sensor edge) (C)":"38.0",'
        '"Temperature (Sensor junction) (C)":"39.0",'
        '"Temperature (Sensor memory) (C)":"58.0",'
        '"GPU use (%)":"7","GPU Memory Allocated (VRAM%)":"13"}}'
    )

    def fake_check_output(cmd, **kwargs):
        assert cmd[0] == "/usr/bin/rocm-smi"
        return rocm_json

    monkeypatch.setattr(sender_v2.subprocess, "check_output", fake_check_output)

    snapshot = sender_v2.get_gpu_snapshot()
    assert snapshot.percent == 7.0
    assert snapshot.temp_c == 38.0
    assert snapshot.mem_percent == 13.0
    assert snapshot.hotspot_c == 39.0
    assert snapshot.mem_temp_c == 58.0


def test_read_metric_float_handles_non_numeric():
    assert sender_v2._read_metric_float("N/A") is None
    assert sender_v2._read_metric_float("38.5 C") == 38.5
    assert sender_v2._read_metric_float(12) == 12.0


def test_get_gpu_snapshot_rocm_handles_variant_keys(monkeypatch):
    monkeypatch.setattr(sender_v2, "_NVIDIA_SMI", None)
    monkeypatch.setattr(sender_v2, "_ROCM_SMI", "/usr/bin/rocm-smi")

    rocm_json = (
        '{"card0":{"Temp Edge (C)":"41.0","Temp Junction (C)":"56.0",'
        '"Temp Memory (C)":"66.0","GPU Busy (%)":"19","VRAM use (%)":"22"}}'
    )

    def fake_check_output(cmd, **kwargs):
        assert cmd[0] == "/usr/bin/rocm-smi"
        return rocm_json

    monkeypatch.setattr(sender_v2.subprocess, "check_output", fake_check_output)

    snapshot = sender_v2.get_gpu_snapshot()
    assert snapshot.percent == 19.0
    assert snapshot.temp_c == 41.0
    assert snapshot.mem_percent == 22.0
    assert snapshot.hotspot_c == 56.0
    assert snapshot.mem_temp_c == 66.0


def test_get_gpu_snapshot_falls_back_to_linux_sysfs(tmp_path, monkeypatch):
    monkeypatch.setattr(sender_v2, "_NVIDIA_SMI", None)
    monkeypatch.setattr(sender_v2, "_ROCM_SMI", None)

    drm_root = tmp_path / "drm"
    card0 = drm_root / "card0"
    device = card0 / "device"
    hwmon = device / "hwmon" / "hwmon9"
    hwmon.mkdir(parents=True)
    (device / "gpu_busy_percent").write_text("37\n", encoding="utf-8")
    (device / "mem_busy_percent").write_text("13\n", encoding="utf-8")
    (hwmon / "temp1_label").write_text("edge\n", encoding="utf-8")
    (hwmon / "temp1_input").write_text("47000\n", encoding="utf-8")
    (hwmon / "temp2_label").write_text("junction\n", encoding="utf-8")
    (hwmon / "temp2_input").write_text("61000\n", encoding="utf-8")
    (hwmon / "temp3_label").write_text("mem\n", encoding="utf-8")
    (hwmon / "temp3_input").write_text("65000\n", encoding="utf-8")

    monkeypatch.setattr(sender_v2, "_DRM_CLASS_ROOT", drm_root, raising=False)

    snapshot = sender_v2.get_gpu_snapshot()
    assert snapshot.percent == 37.0
    assert snapshot.temp_c == 47.0
    assert snapshot.mem_percent == 13.0
    assert snapshot.hotspot_c == 61.0
    assert snapshot.mem_temp_c == 65.0


def test_get_gpu_snapshot_prefers_more_active_sysfs_card(tmp_path, monkeypatch):
    monkeypatch.setattr(sender_v2, "_NVIDIA_SMI", None)
    monkeypatch.setattr(sender_v2, "_ROCM_SMI", None)

    drm_root = tmp_path / "drm"

    card0_device = drm_root / "card0" / "device"
    card0_hwmon = card0_device / "hwmon" / "hwmon1"
    card0_hwmon.mkdir(parents=True)
    (card0_device / "gpu_busy_percent").write_text("0\n", encoding="utf-8")
    (card0_hwmon / "temp1_label").write_text("edge\n", encoding="utf-8")
    (card0_hwmon / "temp1_input").write_text("37000\n", encoding="utf-8")

    card1_device = drm_root / "card1" / "device"
    card1_hwmon = card1_device / "hwmon" / "hwmon2"
    card1_hwmon.mkdir(parents=True)
    (card1_device / "gpu_busy_percent").write_text("57\n", encoding="utf-8")
    (card1_hwmon / "temp1_label").write_text("edge\n", encoding="utf-8")
    (card1_hwmon / "temp1_input").write_text("43000\n", encoding="utf-8")

    monkeypatch.setattr(sender_v2, "_DRM_CLASS_ROOT", drm_root, raising=False)

    snapshot = sender_v2.get_gpu_snapshot()
    assert snapshot.percent == 57.0
    assert snapshot.temp_c == 43.0
