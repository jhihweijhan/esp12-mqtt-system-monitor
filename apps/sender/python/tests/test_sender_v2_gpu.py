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
