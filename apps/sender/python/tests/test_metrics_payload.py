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


def test_topic_for_host_v2():
    assert topic_for_host("desk") == "sys/agents/desk/metrics/v2"


def test_build_payload_shape():
    snapshot = MetricsSnapshot(
        hostname="desk",
        ts_ms=1700000000000,
        cpu=CpuSnapshot(percent=42.37, temp_c=58.2),
        ram=RamSnapshot(percent=67.8, used_mb=12288, total_mb=32768),
        gpu=GpuSnapshot(percent=15.0, temp_c=52.0, mem_percent=12.5, hotspot_c=0.0, mem_temp_c=0.0),
        net=NetSnapshot(rx_kbps=1024, tx_kbps=512),
        disk=DiskSnapshot(read_kBps=2048, write_kBps=1024),
    )

    payload = build_payload(snapshot)

    assert payload["v"] == 2
    assert payload["h"] == "desk"
    assert payload["cpu"] == [42.4, 58.2]
    assert payload["ram"] == [67.8, 12288, 32768]
    assert payload["gpu"] == [15.0, 52.0, 12.5, 0.0, 0.0]
    assert payload["net"] == [1024, 512]
    assert payload["disk"] == [2048, 1024]
