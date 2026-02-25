"""Metrics v2 payload builder shared by sender implementations."""

from __future__ import annotations

from dataclasses import dataclass
from time import time


@dataclass
class CpuSnapshot:
    percent: float = 0.0
    temp_c: float = 0.0


@dataclass
class RamSnapshot:
    percent: float = 0.0
    used_mb: int = 0
    total_mb: int = 0


@dataclass
class GpuSnapshot:
    percent: float = 0.0
    temp_c: float = 0.0
    mem_percent: float = 0.0
    hotspot_c: float = 0.0
    mem_temp_c: float = 0.0


@dataclass
class NetSnapshot:
    rx_kbps: int = 0
    tx_kbps: int = 0


@dataclass
class DiskSnapshot:
    read_kBps: int = 0
    write_kBps: int = 0


@dataclass
class MetricsSnapshot:
    hostname: str
    cpu: CpuSnapshot
    ram: RamSnapshot
    gpu: GpuSnapshot
    net: NetSnapshot
    disk: DiskSnapshot
    ts_ms: int | None = None


def topic_for_host(hostname: str) -> str:
    return f"sys/agents/{hostname}/metrics/v2"


def build_payload(snapshot: MetricsSnapshot) -> dict:
    timestamp = snapshot.ts_ms if snapshot.ts_ms is not None else int(time() * 1000)
    return {
        "v": 2,
        "ts": timestamp,
        "h": snapshot.hostname,
        "cpu": [round(snapshot.cpu.percent, 1), round(snapshot.cpu.temp_c, 1)],
        "ram": [round(snapshot.ram.percent, 1), int(snapshot.ram.used_mb), int(snapshot.ram.total_mb)],
        "gpu": [
            round(snapshot.gpu.percent, 1),
            round(snapshot.gpu.temp_c, 1),
            round(snapshot.gpu.mem_percent, 1),
            round(snapshot.gpu.hotspot_c, 1),
            round(snapshot.gpu.mem_temp_c, 1),
        ],
        "net": [int(snapshot.net.rx_kbps), int(snapshot.net.tx_kbps)],
        "disk": [int(snapshot.disk.read_kBps), int(snapshot.disk.write_kBps)],
    }
