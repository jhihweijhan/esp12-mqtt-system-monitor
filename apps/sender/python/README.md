# Python Sender v2

這是把電腦系統資訊送到 MQTT 的發送器（Ubuntu / Windows 可用）。

## 推薦啟動方式（Docker Compose，適合一般使用者）

```bash
cd apps/sender/python
chmod +x senderctl.sh
./senderctl.sh quickstart-compose
```

第一次會進設定精靈，完成後就會常駐執行。

### 常用命令

```bash
./senderctl.sh compose-status
./senderctl.sh compose-logs
./senderctl.sh compose-restart
./senderctl.sh compose-down
```

如果要確保重開機後自動啟動 Docker：

```bash
./senderctl.sh ensure-docker-boot
```

## 進階模式（本機直接跑）

```bash
uv sync
MQTT_HOST=127.0.0.1 MQTT_PORT=1883 MQTT_USER=your_user MQTT_PASS=your_pass uv run python sender_v2.py
```

## 測試

```bash
uv sync --extra dev
uv run python -m pytest -q
```

## GPU 指標來源（自動偵測）

- NVIDIA：`nvidia-smi`
- AMD：`rocm-smi --json`
- Linux 原生 fallback：`/sys/class/drm/card*/device/*`
  - `gpu_busy_percent`
  - `mem_busy_percent`
  - `hwmon temp*_input`

若所有來源都不可用，GPU 欄位會回傳 `0`。

## 輸出 Topic

- `sys/agents/<hostname>/metrics/v2`
