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
    edge_temp = _read_metric_float(first_gpu.get("Temperature (Sensor edge) (C)"))
    junction_temp = _read_metric_float(first_gpu.get("Temperature (Sensor junction) (C)"))
    mem_temp = _read_metric_float(first_gpu.get("Temperature (Sensor memory) (C)"))
    mem_pct = _read_metric_float(first_gpu.get("GPU Memory Allocated (VRAM%)"))

    if usage is None and edge_temp is None and junction_temp is None and mem_pct is None:
        return None

    return GpuSnapshot(
        percent=usage if usage is not None else 0.0,
        temp_c=edge_temp if edge_temp is not None else (junction_temp if junction_temp is not None else 0.0),
        mem_percent=mem_pct if mem_pct is not None else 0.0,
        hotspot_c=junction_temp if junction_temp is not None else 0.0,
        mem_temp_c=mem_temp if mem_temp is not None else 0.0,
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
