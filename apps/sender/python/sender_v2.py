#!/usr/bin/env python3
"""Cross-platform metrics sender (Ubuntu/Windows) for ESP firmware metrics v2."""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import time
import re
from dataclasses import dataclass
from pathlib import Path

import psutil
from paho.mqtt import client as mqtt

from metrics_payload import (
    CpuSnapshot,
    DiskSnapshot,
    GpuSnapshot,
    MetricsSnapshot,
    NetSnapshot,
    RamSnapshot,
    build_payload,
    topic_for_host,
)

_NVIDIA_SMI = shutil.which("nvidia-smi")
_ROCM_SMI = shutil.which("rocm-smi")
_DRM_CLASS_ROOT = Path(os.getenv("DRM_CLASS_ROOT", "/sys/class/drm"))


@dataclass
class RateSampleState:
    net_rx_bytes: int
    net_tx_bytes: int
    disk_read_bytes: int
    disk_write_bytes: int
    ts: float


class RateSampler:
    def __init__(self) -> None:
        self._state = self._read_state()

    @staticmethod
    def _read_state() -> RateSampleState:
        net = psutil.net_io_counters()
        disk = psutil.disk_io_counters() or type("Disk", (), {"read_bytes": 0, "write_bytes": 0})()
        return RateSampleState(
            net_rx_bytes=int(net.bytes_recv),
            net_tx_bytes=int(net.bytes_sent),
            disk_read_bytes=int(getattr(disk, "read_bytes", 0)),
            disk_write_bytes=int(getattr(disk, "write_bytes", 0)),
            ts=time.time(),
        )

    def sample(self) -> tuple[NetSnapshot, DiskSnapshot]:
        current = self._read_state()
        elapsed = max(current.ts - self._state.ts, 1e-3)

        net_rx_kbps = int(max(current.net_rx_bytes - self._state.net_rx_bytes, 0) / elapsed / 1024.0)
        net_tx_kbps = int(max(current.net_tx_bytes - self._state.net_tx_bytes, 0) / elapsed / 1024.0)
        disk_read_kBps = int(max(current.disk_read_bytes - self._state.disk_read_bytes, 0) / elapsed / 1024.0)
        disk_write_kBps = int(max(current.disk_write_bytes - self._state.disk_write_bytes, 0) / elapsed / 1024.0)

        self._state = current

        return (
            NetSnapshot(rx_kbps=net_rx_kbps, tx_kbps=net_tx_kbps),
            DiskSnapshot(read_kBps=disk_read_kBps, write_kBps=disk_write_kBps),
        )


def get_cpu_temp_c() -> float:
    try:
        temps = psutil.sensors_temperatures() or {}
    except (AttributeError, OSError):
        return 0.0

    for sensor_name in ("k10temp", "coretemp", "cpu_thermal"):
        if sensor_name in temps and temps[sensor_name]:
            return float(temps[sensor_name][0].current)

    for entries in temps.values():
        if entries:
            return float(entries[0].current)

    return 0.0


def get_gpu_snapshot() -> GpuSnapshot:
    nvidia_snapshot = _read_gpu_snapshot_nvidia()
    if nvidia_snapshot is not None:
        return nvidia_snapshot

    rocm_snapshot = _read_gpu_snapshot_rocm()
    if rocm_snapshot is not None:
        return rocm_snapshot

    linux_sysfs_snapshot = _read_gpu_snapshot_linux_sysfs()
    if linux_sysfs_snapshot is not None:
        return linux_sysfs_snapshot

    return GpuSnapshot()


def _read_gpu_snapshot_nvidia() -> GpuSnapshot | None:
    if not _NVIDIA_SMI:
        return None

    query = (
        _NVIDIA_SMI,
        "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
        "--format=csv,noheader,nounits",
    )

    try:
        raw = subprocess.check_output(query, stderr=subprocess.DEVNULL, text=True, timeout=1.0).strip()
    except (subprocess.SubprocessError, OSError):
        return None

    if not raw:
        return None

    first_line = raw.splitlines()[0]
    parts = [p.strip() for p in first_line.split(",")]
    if len(parts) < 4:
        return None

    try:
        usage = float(parts[0])
        temp = float(parts[1])
        mem_used = float(parts[2])
        mem_total = float(parts[3])
        mem_percent = (mem_used / mem_total * 100.0) if mem_total > 0 else 0.0
    except ValueError:
        return None

    return GpuSnapshot(percent=usage, temp_c=temp, mem_percent=mem_percent)


def _read_gpu_snapshot_rocm() -> GpuSnapshot | None:
    if not _ROCM_SMI:
        return None

    query = (_ROCM_SMI, "--showuse", "--showtemp", "--showmemuse", "--json")
    try:
        raw = subprocess.check_output(query, stderr=subprocess.DEVNULL, text=True, timeout=1.0).strip()
    except (subprocess.SubprocessError, OSError):
        return None

    if not raw:
        return None

    try:
        parsed = json.loads(raw)
    except ValueError:
        return None

    if not isinstance(parsed, dict) or not parsed:
        return None

    first_gpu = None
    for key, value in parsed.items():
        if isinstance(value, dict) and str(key).lower().startswith("card"):
            first_gpu = value
            break
    if first_gpu is None:
        return None

    usage = _read_metric_float(first_gpu.get("GPU use (%)"))
    if usage is None:
        usage = _read_metric_by_keys(first_gpu, include_all=("gpu",), include_any=("use", "busy", "util"))

    edge_temp = _read_metric_float(first_gpu.get("Temperature (Sensor edge) (C)"))
    if edge_temp is None:
        edge_temp = _read_metric_by_keys(first_gpu, include_all=("temp",), include_any=("edge",))

    junction_temp = _read_metric_float(first_gpu.get("Temperature (Sensor junction) (C)"))
    if junction_temp is None:
        junction_temp = _read_metric_by_keys(first_gpu, include_all=("temp",), include_any=("junction", "hotspot", "hot"))

    mem_temp = _read_metric_float(first_gpu.get("Temperature (Sensor memory) (C)"))
    if mem_temp is None:
        mem_temp = _read_metric_by_keys(first_gpu, include_all=("temp",), include_any=("memory", "mem"))

    mem_pct = _read_metric_float(first_gpu.get("GPU Memory Allocated (VRAM%)"))
    if mem_pct is None:
        mem_pct = _read_metric_by_keys(first_gpu, include_all=("vram",), include_any=("%", "alloc", "use", "used"))

    if usage is None and edge_temp is None and junction_temp is None and mem_pct is None:
        return None

    return GpuSnapshot(
        percent=usage if usage is not None else 0.0,
        temp_c=edge_temp if edge_temp is not None else (junction_temp if junction_temp is not None else 0.0),
        mem_percent=mem_pct if mem_pct is not None else 0.0,
        hotspot_c=junction_temp if junction_temp is not None else 0.0,
        mem_temp_c=mem_temp if mem_temp is not None else 0.0,
    )


def _read_gpu_snapshot_linux_sysfs() -> GpuSnapshot | None:
    try:
        cards = sorted(_DRM_CLASS_ROOT.iterdir(), key=lambda p: p.name)
    except OSError:
        return None

    best_snapshot = None
    best_rank = None

    for card in cards:
        if not card.is_dir():
            continue
        if not re.fullmatch(r"card\d+", card.name):
            continue

        snapshot = _read_gpu_snapshot_linux_sysfs_card(card)
        if snapshot is None:
            continue

        rank = _gpu_snapshot_rank(snapshot)
        if best_snapshot is None or (best_rank is not None and rank > best_rank):
            best_snapshot = snapshot
            best_rank = rank

    return best_snapshot


def _read_gpu_snapshot_linux_sysfs_card(card_path: Path) -> GpuSnapshot | None:
    device_path = card_path / "device"
    if not device_path.exists():
        return None

    usage = _read_sysfs_float(device_path / "gpu_busy_percent")
    if usage is None:
        usage = _read_sysfs_float(device_path / "gt_busy_percent")

    mem_pct = _read_sysfs_vram_percent(device_path)
    if mem_pct is None:
        mem_pct = _read_sysfs_float(device_path / "mem_busy_percent")

    edge_temp, junction_temp, mem_temp = _read_sysfs_gpu_temps(device_path)

    if usage is None and edge_temp is None and junction_temp is None and mem_pct is None and mem_temp is None:
        return None

    return GpuSnapshot(
        percent=usage if usage is not None else 0.0,
        temp_c=edge_temp if edge_temp is not None else (junction_temp if junction_temp is not None else 0.0),
        mem_percent=mem_pct if mem_pct is not None else 0.0,
        hotspot_c=junction_temp if junction_temp is not None else 0.0,
        mem_temp_c=mem_temp if mem_temp is not None else 0.0,
    )


def _read_sysfs_vram_percent(device_path: Path) -> float | None:
    used = _read_sysfs_float(device_path / "mem_info_vram_used")
    total = _read_sysfs_float(device_path / "mem_info_vram_total")
    if used is None or total is None or total <= 0:
        return None
    return used / total * 100.0


def _read_sysfs_gpu_temps(device_path: Path) -> tuple[float | None, float | None, float | None]:
    edge_temp = None
    junction_temp = None
    mem_temp = None

    hwmon_root = device_path / "hwmon"
    try:
        hwmon_dirs = sorted(hwmon_root.glob("hwmon*"))
    except OSError:
        return None, None, None

    for hwmon_dir in hwmon_dirs:
        if not hwmon_dir.is_dir():
            continue

        try:
            temp_inputs = sorted(hwmon_dir.glob("temp*_input"))
        except OSError:
            continue

        for input_path in temp_inputs:
            raw_temp = _read_sysfs_float(input_path)
            if raw_temp is None:
                continue
            temp_c = _normalize_temp_c(raw_temp)

            label_path = Path(str(input_path).replace("_input", "_label"))
            label = ""
            try:
                label = label_path.read_text(encoding="utf-8").strip().lower()
            except OSError:
                pass

            if "junction" in label or "hotspot" in label or label == "hot":
                if junction_temp is None:
                    junction_temp = temp_c
                continue
            if "edge" in label:
                if edge_temp is None:
                    edge_temp = temp_c
                continue
            if "mem" in label:
                if mem_temp is None:
                    mem_temp = temp_c
                continue

            if edge_temp is None:
                edge_temp = temp_c
            elif junction_temp is None:
                junction_temp = temp_c
            elif mem_temp is None:
                mem_temp = temp_c

    return edge_temp, junction_temp, mem_temp


def _normalize_temp_c(raw_value: float) -> float:
    if abs(raw_value) >= 200.0:
        return raw_value / 1000.0
    return raw_value


def _read_sysfs_float(path: Path) -> float | None:
    try:
        content = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    if not content:
        return None
    try:
        return float(content)
    except ValueError:
        return None


def _read_metric_by_keys(
    metrics: dict[str, object], include_all: tuple[str, ...], include_any: tuple[str, ...]
) -> float | None:
    for key, value in metrics.items():
        key_l = str(key).lower()
        if not all(token in key_l for token in include_all):
            continue
        if include_any and not any(token in key_l for token in include_any):
            continue
        parsed = _read_metric_float(value)
        if parsed is not None:
            return parsed
    return None


def _gpu_snapshot_rank(snapshot: GpuSnapshot) -> tuple[float, float, float, float, float, float, float]:
    return (
        1.0 if snapshot.percent > 0 else 0.0,
        snapshot.percent,
        1.0 if snapshot.mem_percent > 0 else 0.0,
        snapshot.mem_percent,
        snapshot.hotspot_c,
        snapshot.temp_c,
        snapshot.mem_temp_c,
    )


def _read_metric_float(value: object) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value)
    match = re.search(r"-?\d+(?:\.\d+)?", text)
    if not match:
        return None
    try:
        return float(match.group(0))
    except ValueError:
        return None


def build_snapshot(hostname: str, rate_sampler: RateSampler) -> MetricsSnapshot:
    vm = psutil.virtual_memory()
    net, disk = rate_sampler.sample()

    return MetricsSnapshot(
        hostname=hostname,
        ts_ms=int(time.time() * 1000),
        cpu=CpuSnapshot(percent=float(psutil.cpu_percent(interval=None)), temp_c=get_cpu_temp_c()),
        ram=RamSnapshot(
            percent=float(vm.percent),
            used_mb=int(vm.used / 1024 / 1024),
            total_mb=int(vm.total / 1024 / 1024),
        ),
        gpu=get_gpu_snapshot(),
        net=net,
        disk=disk,
    )


def read_env(name: str, default: str) -> str:
    value = os.getenv(name)
    return value if value is not None and value != "" else default


def create_mqtt_client(client_id: str) -> tuple[mqtt.Client, str, int]:
    mqtt_host = read_env("MQTT_HOST", "127.0.0.1")
    mqtt_port = int(read_env("MQTT_PORT", "1883"))
    mqtt_user = read_env("MQTT_USER", "")
    mqtt_pass = read_env("MQTT_PASS", "")

    client_kwargs: dict = {"client_id": client_id, "protocol": mqtt.MQTTv311}
    if hasattr(mqtt, "CallbackAPIVersion"):
        client_kwargs["callback_api_version"] = mqtt.CallbackAPIVersion.VERSION2

    client = mqtt.Client(**client_kwargs)
    if mqtt_user:
        client.username_pw_set(mqtt_user, mqtt_pass)

    return client, mqtt_host, mqtt_port


def connect_mqtt_with_retry(client: mqtt.Client, mqtt_host: str, mqtt_port: int) -> None:
    attempt = 0
    while True:
        try:
            client.connect(mqtt_host, mqtt_port, keepalive=30)
            return
        except OSError as exc:
            delay_sec = min(30, 2 ** min(attempt, 5))
            print(
                f"MQTT connect failed ({mqtt_host}:{mqtt_port}) -> {exc}. "
                f"Retrying in {delay_sec}s..."
            )
            time.sleep(delay_sec)
            attempt += 1


def parse_interval(raw: str) -> float:
    try:
        interval = float(raw)
    except ValueError:
        return 1.0
    return max(interval, 0.2)


def parse_qos(raw: str) -> int:
    try:
        qos = int(raw)
    except ValueError:
        return 0
    return max(0, min(qos, 2))


def connect_and_start(client: mqtt.Client, mqtt_host: str, mqtt_port: int) -> None:
    connect_mqtt_with_retry(client, mqtt_host, mqtt_port)
    client.loop_start()


def main() -> int:
    hostname = read_env("SENDER_HOSTNAME", socket.gethostname())
    interval = parse_interval(read_env("SEND_INTERVAL_SEC", "1.0"))
    qos = parse_qos(read_env("MQTT_QOS", "0"))

    topic = topic_for_host(hostname)
    rate_sampler = RateSampler()
    client, mqtt_host, mqtt_port = create_mqtt_client(f"sender-v2-{hostname}")
    connect_and_start(client, mqtt_host, mqtt_port)

    print(
        f"Sender v2 started: host={hostname} topic={topic} "
        f"broker={mqtt_host}:{mqtt_port} interval={interval}s"
    )

    # Warm up cpu_percent baseline so first sample is meaningful.
    psutil.cpu_percent(interval=None)

    try:
        while True:
            snapshot = build_snapshot(hostname, rate_sampler)
            payload = build_payload(snapshot)
            encoded = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
            info = client.publish(topic, payload=encoded, qos=qos, retain=False)
            if info.rc != mqtt.MQTT_ERR_SUCCESS:
                print(f"publish failed rc={info.rc}")
            time.sleep(interval)
    except KeyboardInterrupt:
        print("Sender v2 stopped")
    finally:
        client.loop_stop()
        client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
