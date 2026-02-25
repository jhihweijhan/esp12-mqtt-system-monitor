# Metrics v2 Protocol

## Topic

- Sender publish topic:
  - `sys/agents/<hostname>/metrics/v2`

## Payload

Compact JSON with fixed keys and fixed array positions.

```json
{
  "v": 2,
  "ts": 1739999999000,
  "h": "desk",
  "cpu": [42.4, 58.2],
  "ram": [67.8, 12288, 32768],
  "gpu": [15.0, 52.0, 12.5, 0.0, 0.0],
  "net": [1024, 512],
  "disk": [2048, 1024]
}
```

## Field definitions

- `v`: schema version, must be `2`
- `ts`: sender unix epoch milliseconds
- `h`: sender hostname
- `cpu`: `[cpu_percent, cpu_temp_c]`
- `ram`: `[ram_percent, ram_used_mb, ram_total_mb]`
- `gpu`: `[gpu_percent, gpu_temp_c, gpu_mem_percent, gpu_hotspot_c, gpu_mem_temp_c]`
- `net`: `[rx_kbps, tx_kbps]`
- `disk`: `[read_kBps, write_kBps]`

## Rules

- Firmware only accepts `/metrics/v2` topics.
- Firmware rejects payloads where `v != 2`.
- Recommended sender frequency: `1Hz` per host.
- Sender should always send all arrays; when unavailable, send `0` for values.
