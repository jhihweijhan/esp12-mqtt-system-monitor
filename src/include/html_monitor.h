#ifndef HTML_MONITOR_H
#define HTML_MONITOR_H

const char HTML_MONITOR[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP12 Monitor</title>
    <style>
        * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
        body { margin: 0; padding: 16px; background: #0f172a; color: #e2e8f0; min-height: 100vh; }
        .container { max-width: 480px; margin: 0 auto; }
        h1 { text-align: center; color: #38bdf8; font-size: 22px; margin-bottom: 20px; }
        h2 { color: #94a3b8; font-size: 15px; margin: 0 0 12px 0; }

        .card { background: #1e293b; border-radius: 12px; padding: 16px; margin-bottom: 12px; }
        .form-group { margin-bottom: 14px; }
        label { display: block; margin-bottom: 4px; color: #94a3b8; font-size: 13px; }
        input, select { width: 100%; padding: 10px 12px; font-size: 15px;
            border: 1px solid #334155; border-radius: 8px;
            background: #0f172a; color: #f1f5f9; }
        input:focus, select:focus { outline: none; border-color: #38bdf8; }

        .row { display: flex; gap: 10px; }
        .row > * { flex: 1; }

        button { width: 100%; padding: 14px; font-size: 16px; font-weight: 600;
            border: none; border-radius: 10px; cursor: pointer;
            background: linear-gradient(135deg, #0ea5e9, #2563eb);
            color: #fff; margin-bottom: 8px; }
        button:active { transform: scale(0.98); opacity: 0.9; }
        button.secondary { background: #334155; }

        .device-list { margin-top: 8px; }
        .device-item { display: flex; align-items: center; gap: 8px;
            background: #0f172a; padding: 10px 12px; border-radius: 8px; margin-bottom: 6px; }
        .device-item .name { flex: 1; font-size: 14px; color: #cbd5e1; }
        .device-item .alias { width: 70px; padding: 6px 8px; font-size: 14px; }
        .device-item .time { width: 50px; padding: 6px 8px; font-size: 14px; text-align: center; }
        .device-item input[type="checkbox"] { width: 18px; height: 18px; accent-color: #38bdf8; }

        .status { padding: 10px 14px; border-radius: 8px; margin-top: 12px; display: none; text-align: center; }
        .status.success { display: block; background: #064e3b; color: #34d399; }
        .status.error { display: block; background: #450a0a; color: #f87171; }

        .threshold-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
        .threshold-item label { margin-bottom: 2px; }
        .threshold-item input { padding: 8px; }

        .tabs { display: flex; gap: 2px; margin-bottom: 12px; background: #1e293b; border-radius: 10px; padding: 4px; }
        .tab { flex: 1; padding: 10px; text-align: center; border-radius: 8px;
            cursor: pointer; color: #64748b; font-size: 14px; font-weight: 500; }
        .tab.active { background: #0ea5e9; color: #fff; }
        .tab-content { display: none; }
        .tab-content.active { display: block; }

        .hint { font-size: 12px; color: #64748b; margin-top: 4px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Monitor Settings</h1>

        <div class="tabs">
            <div class="tab active" onclick="showTab('mqtt')">MQTT</div>
            <div class="tab" onclick="showTab('devices')">Devices</div>
            <div class="tab" onclick="showTab('display')">Display</div>
        </div>

        <div id="tab-mqtt" class="tab-content active">
            <div class="card" style="background:#0f172a;padding:12px;margin-bottom:12px;">
                <div id="mqttStatus" style="text-align:center;font-size:14px;color:#64748b;">
                    Checking MQTT status...
                </div>
            </div>
            <div class="card">
                <h2>MQTT Connection</h2>
                <div class="form-group">
                    <label>Server</label>
                    <input type="text" id="mqttServer" placeholder="192.168.1.100">
                </div>
                <div class="row">
                    <div class="form-group">
                        <label>Port</label>
                        <input type="number" id="mqttPort" value="1883">
                    </div>
                    <div class="form-group">
                        <label>Topic</label>
                        <input type="text" id="mqttTopic" value="sys/agents/+/metrics">
                    </div>
                </div>
                <div class="row">
                    <div class="form-group">
                        <label>Username</label>
                        <input type="text" id="mqttUser" placeholder="optional">
                    </div>
                    <div class="form-group">
                        <label>Password</label>
                        <input type="password" id="mqttPass" placeholder="optional">
                    </div>
                </div>
            </div>
        </div>

        <div id="tab-devices" class="tab-content">
            <div class="card">
                <h2>Device List</h2>
                <p class="hint">Devices are added automatically. Set alias and display time here.</p>
                <div id="deviceList" class="device-list">
                    <p style="color:#475569;">No devices yet</p>
                </div>
            </div>
        </div>

        <div id="tab-display" class="tab-content">
            <div class="card">
                <h2>Carousel</h2>
                <div class="row">
                    <div class="form-group">
                        <label>Default Time (sec)</label>
                        <input type="number" id="displayTime" value="5" min="1" max="60">
                    </div>
                    <div class="form-group">
                        <label>Auto Rotate</label>
                        <select id="autoCarousel">
                            <option value="1">Enabled</option>
                            <option value="0">Disabled</option>
                        </select>
                    </div>
                </div>
            </div>

            <div class="card">
                <h2>Alert Thresholds</h2>
                <div class="threshold-grid">
                    <div class="threshold-item">
                        <label>CPU Warn %</label>
                        <input type="number" id="cpuWarn" value="70" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>CPU Crit %</label>
                        <input type="number" id="cpuCrit" value="90" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>RAM Warn %</label>
                        <input type="number" id="ramWarn" value="70" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>RAM Crit %</label>
                        <input type="number" id="ramCrit" value="90" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>Temp Warn C</label>
                        <input type="number" id="tempWarn" value="60" min="0" max="100">
                    </div>
                    <div class="threshold-item">
                        <label>Temp Crit C</label>
                        <input type="number" id="tempCrit" value="80" min="0" max="100">
                    </div>
                </div>
            </div>
        </div>

        <button onclick="saveConfig()">Save Settings</button>
        <button class="secondary" onclick="loadConfig()">Reload</button>
        <div id="status" class="status"></div>
    </div>

    <script>
        let deviceData = [];

        function showTab(name) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('tab-' + name).classList.add('active');
        }

        function loadConfig() {
            fetch('/api/config')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('mqttServer').value = data.mqtt?.server || '';
                    document.getElementById('mqttPort').value = data.mqtt?.port || 1883;
                    document.getElementById('mqttTopic').value = data.mqtt?.topic || 'sys/agents/+/metrics';
                    document.getElementById('mqttUser').value = data.mqtt?.user || '';
                    document.getElementById('mqttPass').value = '';

                    document.getElementById('displayTime').value = data.displayTime || 5;
                    document.getElementById('autoCarousel').value = data.autoCarousel ? '1' : '0';

                    if (data.thresholds) {
                        document.getElementById('cpuWarn').value = data.thresholds.cpuWarn || 70;
                        document.getElementById('cpuCrit').value = data.thresholds.cpuCrit || 90;
                        document.getElementById('ramWarn').value = data.thresholds.ramWarn || 70;
                        document.getElementById('ramCrit').value = data.thresholds.ramCrit || 90;
                        document.getElementById('tempWarn').value = data.thresholds.tempWarn || 60;
                        document.getElementById('tempCrit').value = data.thresholds.tempCrit || 80;
                    }

                    deviceData = data.devices || [];
                    renderDevices(deviceData);
                })
                .catch(() => {
                    document.getElementById('status').className = 'status error';
                    document.getElementById('status').textContent = 'Failed to load config';
                });
        }

        function renderDevices(devices) {
            const list = document.getElementById('deviceList');
            if (!devices.length) {
                list.innerHTML = '<p style="color:#475569;">No devices yet</p>';
                return;
            }

            list.innerHTML = devices.map((d, i) => `
                <div class="device-item">
                    <input type="checkbox" ${d.enabled ? 'checked' : ''}
                           onchange="updateDevice(${i}, 'enabled', this.checked)">
                    <span class="name">${d.hostname}</span>
                    <input type="text" class="alias" value="${d.alias}"
                           onchange="updateDevice(${i}, 'alias', this.value)" placeholder="Alias">
                    <input type="number" class="time" value="${d.time}" min="1" max="60"
                           onchange="updateDevice(${i}, 'time', this.value)">
                </div>
            `).join('');
        }

        function updateDevice(index, field, value) {
            if (!deviceData[index]) return;
            if (field === 'time') value = parseInt(value);
            if (field === 'enabled') value = !!value;
            deviceData[index][field] = value;
        }

        function saveConfig() {
            const config = {
                mqtt: {
                    server: document.getElementById('mqttServer').value,
                    port: parseInt(document.getElementById('mqttPort').value),
                    topic: document.getElementById('mqttTopic').value,
                    user: document.getElementById('mqttUser').value,
                    pass: document.getElementById('mqttPass').value
                },
                displayTime: parseInt(document.getElementById('displayTime').value),
                autoCarousel: document.getElementById('autoCarousel').value === '1',
                thresholds: {
                    cpuWarn: parseInt(document.getElementById('cpuWarn').value),
                    cpuCrit: parseInt(document.getElementById('cpuCrit').value),
                    ramWarn: parseInt(document.getElementById('ramWarn').value),
                    ramCrit: parseInt(document.getElementById('ramCrit').value),
                    tempWarn: parseInt(document.getElementById('tempWarn').value),
                    tempCrit: parseInt(document.getElementById('tempCrit').value)
                },
                devices: deviceData
            };

            fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(config)
            })
            .then(r => r.json())
            .then(data => {
                const status = document.getElementById('status');
                if (data.success) {
                    status.className = 'status success';
                    status.textContent = 'Saved! Restarting in 3s...';
                    setTimeout(() => location.reload(), 5000);
                } else {
                    status.className = 'status error';
                    status.textContent = 'Save failed: ' + (data.message || 'Unknown error');
                }
            })
            .catch(() => {
                // 連線中斷可能是 ESP 正在重啟（正常行為）
                const status = document.getElementById('status');
                status.className = 'status success';
                status.textContent = 'Saved! ESP restarting... Refresh in 5s';
                setTimeout(() => location.reload(), 5000);
            });
        }

        // 定期更新 MQTT 狀態
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    const mqttStatus = document.getElementById('mqttStatus');
                    if (mqttStatus) {
                        if (data.mqttConnected) {
                            mqttStatus.innerHTML = '<span style="color:#34d399">MQTT Connected</span> - ' +
                                data.onlineCount + ' device(s) online';
                        } else {
                            mqttStatus.innerHTML = '<span style="color:#f87171">MQTT Disconnected</span>';
                        }
                    }
                })
                .catch(() => {});
        }

        loadConfig();
        updateStatus();
        setInterval(updateStatus, 3000);
    </script>
</body>
</html>
)rawliteral";

#endif
