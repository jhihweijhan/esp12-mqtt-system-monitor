#!/usr/bin/env python3
"""Cross-platform metrics sender (Ubuntu/Windows) for ESP firmware metrics v2."""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import time
from dataclasses import dataclass
from typing import Optional

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
    if not shutil.which("nvidia-smi"):
        return GpuSnapshot()

    query = (
        "nvidia-smi",
        "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
        "--format=csv,noheader,nounits",
    )

    try:
        raw = subprocess.check_output(query, stderr=subprocess.DEVNULL, text=True, timeout=1.0).strip()
    except (subprocess.SubprocessError, OSError):
        return GpuSnapshot()

    if not raw:
        return GpuSnapshot()

    first_line = raw.splitlines()[0]
    parts = [p.strip() for p in first_line.split(",")]
    if len(parts) < 4:
        return GpuSnapshot()

    try:
        usage = float(parts[0])
        temp = float(parts[1])
        mem_used = float(parts[2])
        mem_total = float(parts[3])
        mem_percent = (mem_used / mem_total * 100.0) if mem_total > 0 else 0.0
    except ValueError:
        return GpuSnapshot()

    return GpuSnapshot(percent=usage, temp_c=temp, mem_percent=mem_percent)


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


def create_mqtt_client(client_id: str) -> mqtt.Client:
    mqtt_host = read_env("MQTT_HOST", "127.0.0.1")
    mqtt_port = int(read_env("MQTT_PORT", "1883"))
    mqtt_user = read_env("MQTT_USER", "")
    mqtt_pass = read_env("MQTT_PASS", "")

    client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)
    if mqtt_user:
        client.username_pw_set(mqtt_user, mqtt_pass)

    client.connect(mqtt_host, mqtt_port, keepalive=30)
    client.loop_start()
    return client


def main() -> int:
    hostname = read_env("SENDER_HOSTNAME", socket.gethostname())
    interval = float(read_env("SEND_INTERVAL_SEC", "1.0"))
    qos = int(read_env("MQTT_QOS", "0"))

    topic = topic_for_host(hostname)
    rate_sampler = RateSampler()
    client = create_mqtt_client(f"sender-v2-{hostname}")

    print(f"Sender v2 started: host={hostname} topic={topic} interval={interval}s")

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
            time.sleep(max(interval, 0.2))
    except KeyboardInterrupt:
        print("Sender v2 stopped")
    finally:
        client.loop_stop()
        client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
