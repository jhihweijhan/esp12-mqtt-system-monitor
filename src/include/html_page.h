#ifndef HTML_PAGE_H
#define HTML_PAGE_H

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP12 WiFi 設定</title>
    <style>
        * { box-sizing: border-box; font-family: Arial, sans-serif; }
        body { margin: 0; padding: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 400px; margin: 0 auto; }
        h1 { text-align: center; color: #00d9ff; font-size: 24px; }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 8px; color: #aaa; }
        select, input[type="text"] { width: 100%; padding: 12px; font-size: 16px;
            border: 2px solid #333; border-radius: 8px;
            background: #16213e; color: #fff; -webkit-appearance: none; }
        select:focus, input:focus { outline: none; border-color: #00d9ff; }
        button { width: 100%; padding: 14px; font-size: 18px; font-weight: bold;
            border: none; border-radius: 8px; cursor: pointer;
            background: linear-gradient(135deg, #00d9ff, #0095ff);
            color: #fff; transition: transform 0.1s; }
        button:active { transform: scale(0.98); }
        button:disabled { background: #444; cursor: not-allowed; }
        .status { text-align: center; padding: 15px; border-radius: 8px;
            margin-top: 20px; display: none; }
        .status.loading { display: block; background: #16213e; color: #00d9ff; }
        .status.success { display: block; background: #0a3d2a; color: #00ff88; }
        .status.error { display: block; background: #3d0a0a; color: #ff4444; }
        .refresh { margin-top: 10px; font-size: 14px; background: #333; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📶 WiFi 設定</h1>
        <form id="wifiForm">
            <div class="form-group">
                <label>WiFi 名稱</label>
                <select id="ssid" name="ssid" required>
                    <option value="">掃描中...</option>
                </select>
            </div>
            <div class="form-group">
                <label for="pass">密碼</label>
                <input type="text" id="pass" name="pass" placeholder="輸入 WiFi 密碼" autocomplete="off" autocapitalize="off">
            </div>
            <button type="submit" id="submitBtn">儲存並連線</button>
            <button type="button" class="refresh" onclick="scanWiFi()">🔄 重新掃描</button>
        </form>
        <div id="status" class="status"></div>
    </div>
    <script>
        function scanWiFi() {
            document.getElementById('ssid').innerHTML = '<option value="">掃描中...</option>';
            fetch('/scan')
                .then(r => r.json())
                .then(data => {
                    if (data.scanning) {
                        setTimeout(scanWiFi, 1500);
                        return;
                    }
                    if (!Array.isArray(data) || data.length === 0) {
                        document.getElementById('ssid').innerHTML = '<option value="">未找到網路，點擊重新掃描</option>';
                        return;
                    }
                    let opts = '<option value="">選擇 WiFi 網路</option>';
                    data.forEach(n => {
                        const icon = n.secure ? '🔒' : '📶';
                        const signal = n.rssi > -50 ? '▓▓▓' : n.rssi > -70 ? '▓▓░' : '▓░░';
                        opts += `<option value="${n.ssid}">${icon} ${n.ssid} ${signal}</option>`;
                    });
                    document.getElementById('ssid').innerHTML = opts;
                })
                .catch(() => {
                    document.getElementById('ssid').innerHTML = '<option value="">掃描失敗，點擊重新掃描</option>';
                });
        }

        document.getElementById('wifiForm').onsubmit = function(e) {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const pass = document.getElementById('pass').value;
            if (!ssid) { alert('請選擇 WiFi 網路'); return; }

            const status = document.getElementById('status');
            const btn = document.getElementById('submitBtn');
            btn.disabled = true;
            status.className = 'status loading';
            status.textContent = '連線中...';

            fetch('/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    status.className = 'status success';
                    status.innerHTML = `✅ 已連線!<br>IP: ${data.ip}<br>3 秒後重啟...`;
                } else {
                    status.className = 'status error';
                    status.textContent = '❌ 連線失敗: ' + data.message;
                    btn.disabled = false;
                }
            })
            .catch(() => {
                status.className = 'status error';
                status.textContent = '❌ 發生錯誤';
                btn.disabled = false;
            });
        };

        scanWiFi();
    </script>
</body>
</html>
)rawliteral";

#endif
