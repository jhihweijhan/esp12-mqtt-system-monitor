#ifndef HTML_MONITOR_H
#define HTML_MONITOR_H

const char HTML_MONITOR[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP12 Monitor</title>
  <style>
    * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
    body { margin: 0; padding: 16px; background: #0f172a; color: #e2e8f0; min-height: 100vh; }
    .container { max-width: 720px; margin: 0 auto; }
    h1 { text-align: center; color: #38bdf8; font-size: 22px; margin-bottom: 20px; }
    h2 { color: #94a3b8; font-size: 15px; margin: 0 0 12px; }
    .card { background: #1e293b; border-radius: 12px; padding: 16px; margin-bottom: 12px; }
    .form-group { margin-bottom: 14px; }
    label { display: block; margin-bottom: 4px; color: #94a3b8; font-size: 13px; }
    input, select { width: 100%; padding: 10px 12px; font-size: 15px; border: 1px solid #334155; border-radius: 8px; background: #0f172a; color: #f1f5f9; }
    input:focus, select:focus { outline: none; border-color: #38bdf8; }
    .row { display: flex; gap: 10px; }
    .row > * { flex: 1; }
    button { width: 100%; padding: 14px; font-size: 16px; font-weight: 600; border: none; border-radius: 10px; cursor: pointer; background: linear-gradient(135deg, #0ea5e9, #2563eb); color: #fff; margin-bottom: 8px; }
    button:active { transform: scale(.98); opacity: .9; }
    button.secondary { background: #334155; }
    .tabs { display: flex; gap: 2px; margin-bottom: 12px; background: #1e293b; border-radius: 10px; padding: 4px; }
    .tab { flex: 1; padding: 10px; text-align: center; border-radius: 8px; cursor: pointer; color: #64748b; font-size: 14px; font-weight: 500; }
    .tab.active { background: #0ea5e9; color: #fff; }
    .tab-content { display: none; }
    .tab-content.active { display: block; }
    .hint { font-size: 12px; color: #64748b; margin-top: 4px; }
    .status { padding: 10px 14px; border-radius: 8px; margin-top: 12px; display: none; text-align: center; }
    .status.success { display: block; background: #064e3b; color: #34d399; }
    .status.error { display: block; background: #450a0a; color: #f87171; }
    .topic-list { display: grid; gap: 8px; }
    .topic-item { display: grid; grid-template-columns: 28px 1fr 140px 72px; gap: 8px; align-items: center; background: #0f172a; padding: 10px 12px; border-radius: 8px; }
    .topic-item .topic { color: #cbd5e1; font-size: 13px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .topic-item input[type=checkbox] { width: 18px; height: 18px; accent-color: #38bdf8; }
    .topic-item .alias, .topic-item .time { padding: 6px 8px; font-size: 14px; }
    .topic-item .time { text-align: center; }
    @media (max-width: 700px) {
      .topic-item { grid-template-columns: 28px 1fr; }
      .topic-item .alias, .topic-item .time { width: 100%; }
      .topic-item .topic { grid-column: 2; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Monitor Settings</h1>

    <div class="tabs">
      <div class="tab active" onclick="showTab(event,'mqtt')">MQTT</div>
      <div class="tab" onclick="showTab(event,'topics')">Topics</div>
      <div class="tab" onclick="showTab(event,'display')">Display</div>
    </div>

    <div id="tab-mqtt" class="tab-content active">
      <div class="card" style="background:#0f172a;padding:12px;margin-bottom:12px">
        <div id="mqttStatus" style="text-align:center;font-size:14px;color:#64748b">Checking...</div>
      </div>
      <div class="card">
        <h2>MQTT Connection</h2>
        <div class="form-group"><label>Server</label><input type="text" id="mqttServer" placeholder="192.168.1.100"></div>
        <div class="row">
          <div class="form-group"><label>Port</label><input type="number" id="mqttPort" value="1883"></div>
          <div class="form-group"><label>Topic Pattern (legacy)</label><input type="text" id="mqttTopic" value="sys/agents/+/metrics"></div>
        </div>
        <div class="row">
          <div class="form-group"><label>Username</label><input type="text" id="mqttUser" placeholder="optional"></div>
          <div class="form-group"><label>Password</label><input type="password" id="mqttPass" placeholder="optional"></div>
        </div>
      </div>
    </div>

    <div id="tab-topics" class="tab-content">
      <div class="card">
        <h2>Sender Topic Subscriptions</h2>
        <p class="hint">Select sender topics to subscribe. Alias is still customizable for display.</p>
        <div id="topicList" class="topic-list"><p style="color:#475569">No topics discovered yet</p></div>
      </div>
    </div>

    <div id="tab-display" class="tab-content">
      <div class="card">
        <h2>Carousel</h2>
        <div class="row">
          <div class="form-group"><label>Default Time (sec)</label><input type="number" id="displayTime" value="5" min="1" max="60"></div>
          <div class="form-group"><label>Auto Rotate</label><select id="autoCarousel"><option value="1">Enabled</option><option value="0">Disabled</option></select></div>
        </div>
        <div class="form-group"><label>Offline Timeout (sec)</label><input type="number" id="offlineTimeoutSec" value="20" min="5" max="300"></div>
      </div>
      <div class="card">
        <h2>Alert Thresholds</h2>
        <div style="display:grid;grid-template-columns:repeat(2,1fr);gap:10px">
          <div class="form-group"><label>CPU Warn %</label><input type="number" id="cpuWarn" value="70" min="0" max="100"></div>
          <div class="form-group"><label>CPU Crit %</label><input type="number" id="cpuCrit" value="90" min="0" max="100"></div>
          <div class="form-group"><label>RAM Warn %</label><input type="number" id="ramWarn" value="70" min="0" max="100"></div>
          <div class="form-group"><label>RAM Crit %</label><input type="number" id="ramCrit" value="90" min="0" max="100"></div>
          <div class="form-group"><label>Temp Warn C</label><input type="number" id="tempWarn" value="60" min="0" max="100"></div>
          <div class="form-group"><label>Temp Crit C</label><input type="number" id="tempCrit" value="80" min="0" max="100"></div>
        </div>
      </div>
    </div>

    <button onclick="saveConfig()">Save Settings</button>
    <button class="secondary" onclick="loadConfig()">Reload</button>
    <div id="status" class="status"></div>
  </div>

  <script>
    let D = [];
    let topicRows = [];
    let SI = 0;

    function showTab(evt, name) {
      document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
      document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
      evt.currentTarget.classList.add('active');
      document.getElementById('tab-' + name).classList.add('active');
    }

    function topicFromHost(hostname) {
      return 'sys/agents/' + hostname + '/metrics';
    }

    function hostFromTopic(topic) {
      if (!topic) return '';
      const m = topic.match(/^sys\/agents\/([^\/]+)\/metrics$/);
      return m ? m[1] : '';
    }

    function buildTopicRows(cfg) {
      const devMap = {};
      (cfg.devices || []).forEach(d => {
        if (!d || !d.hostname) return;
        devMap[d.hostname] = {
          alias: d.alias || d.hostname,
          time: d.time || cfg.displayTime || 5,
          enabled: !!d.enabled
        };
      });

      const subscribed = new Set(cfg.mqtt?.subscribedTopics || []);
      const topics = new Set();
      (cfg.mqtt?.availableTopics || []).forEach(t => topics.add(t));
      (cfg.mqtt?.subscribedTopics || []).forEach(t => topics.add(t));
      (cfg.devices || []).forEach(d => {
        if (d && d.hostname) topics.add(topicFromHost(d.hostname));
      });

      topicRows = [];
      Array.from(topics).sort().forEach(topic => {
        const hostname = hostFromTopic(topic);
        if (!hostname) return;
        const known = devMap[hostname] || { alias: hostname, time: cfg.displayTime || 5, enabled: false };
        topicRows.push({
          topic: topic,
          hostname: hostname,
          checked: subscribed.has(topic),
          alias: known.alias,
          time: known.time
        });
      });
    }

    function renderTopicRows() {
      const list = document.getElementById('topicList');
      if (!topicRows.length) {
        list.innerHTML = '<p style="color:#475569">No topics discovered yet</p>';
        return;
      }
      list.innerHTML = topicRows.map((r, i) =>
        '<div class="topic-item">' +
          '<input type="checkbox" ' + (r.checked ? 'checked' : '') + ' onchange="updateTopicRow(' + i + ',\'checked\',this.checked)">' +
          '<div class="topic">' + r.topic + '</div>' +
          '<input class="alias" type="text" value="' + (r.alias || '') + '" placeholder="Alias" oninput="updateTopicRow(' + i + ',\'alias\',this.value)">' +
          '<input class="time" type="number" value="' + (r.time || 5) + '" min="1" max="60" oninput="updateTopicRow(' + i + ',\'time\',this.value)">' +
        '</div>'
      ).join('');
    }

    function updateTopicRow(index, field, value) {
      if (!topicRows[index]) return;
      if (field === 'time') value = parseInt(value, 10) || 5;
      if (field === 'checked') value = !!value;
      topicRows[index][field] = value;
    }

    function loadConfig() {
      fetch('/api/config')
        .then(r => r.json())
        .then(cfg => {
          document.getElementById('mqttServer').value = cfg.mqtt?.server || '';
          document.getElementById('mqttPort').value = cfg.mqtt?.port || 1883;
          document.getElementById('mqttTopic').value = cfg.mqtt?.topic || 'sys/agents/+/metrics';
          document.getElementById('mqttUser').value = cfg.mqtt?.user || '';
          document.getElementById('mqttPass').value = '';

          document.getElementById('displayTime').value = cfg.displayTime || 5;
          document.getElementById('autoCarousel').value = cfg.autoCarousel ? '1' : '0';
          document.getElementById('offlineTimeoutSec').value = cfg.offlineTimeoutSec || 20;

          if (cfg.thresholds) {
            document.getElementById('cpuWarn').value = cfg.thresholds.cpuWarn || 70;
            document.getElementById('cpuCrit').value = cfg.thresholds.cpuCrit || 90;
            document.getElementById('ramWarn').value = cfg.thresholds.ramWarn || 70;
            document.getElementById('ramCrit').value = cfg.thresholds.ramCrit || 90;
            document.getElementById('tempWarn').value = cfg.thresholds.tempWarn || 60;
            document.getElementById('tempCrit').value = cfg.thresholds.tempCrit || 80;
          }

          D = cfg.devices || [];
          buildTopicRows(cfg);
          renderTopicRows();
        })
        .catch(() => {
          const s = document.getElementById('status');
          s.className = 'status error';
          s.textContent = 'Failed to load';
        });
    }

    function buildSavePayload() {
      const subscribedTopics = [];
      const devices = [];

      topicRows.forEach(r => {
        if (!r.hostname) return;
        devices.push({
          hostname: r.hostname,
          alias: (r.alias || r.hostname).trim(),
          time: parseInt(r.time, 10) || 5,
          enabled: !!r.checked
        });
        if (r.checked) subscribedTopics.push(r.topic);
      });

      return {
        mqtt: {
          server: document.getElementById('mqttServer').value,
          port: parseInt(document.getElementById('mqttPort').value, 10),
          topic: document.getElementById('mqttTopic').value,
          user: document.getElementById('mqttUser').value,
          pass: document.getElementById('mqttPass').value,
          subscribedTopics: subscribedTopics
        },
        displayTime: parseInt(document.getElementById('displayTime').value, 10),
        autoCarousel: document.getElementById('autoCarousel').value === '1',
        offlineTimeoutSec: parseInt(document.getElementById('offlineTimeoutSec').value, 10) || 20,
        thresholds: {
          cpuWarn: parseInt(document.getElementById('cpuWarn').value, 10),
          cpuCrit: parseInt(document.getElementById('cpuCrit').value, 10),
          ramWarn: parseInt(document.getElementById('ramWarn').value, 10),
          ramCrit: parseInt(document.getElementById('ramCrit').value, 10),
          tempWarn: parseInt(document.getElementById('tempWarn').value, 10),
          tempCrit: parseInt(document.getElementById('tempCrit').value, 10)
        },
        devices: devices
      };
    }

    function doSave(body, retry) {
      const s = document.getElementById('status');
      s.className = 'status';
      s.style.display = 'block';
      s.style.background = '#1e293b';
      s.style.color = '#94a3b8';
      s.textContent = retry > 0 ? 'Retrying...' : 'Saving...';

      fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: body
      })
      .then(r => {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
      })
      .then(d => {
        if (d.success) {
          s.className = 'status success';
          s.textContent = 'Saved! Restarting...';
          setTimeout(() => location.reload(), 8000);
        } else {
          s.className = 'status error';
          s.textContent = 'Save failed: ' + (d.message || 'unknown');
        }
      })
      .catch(e => {
        if (retry < 2) {
          setTimeout(() => doSave(body, retry + 1), 2000);
        } else {
          s.className = 'status error';
          s.textContent = 'Save failed: ' + e.message;
        }
      });
    }

    function saveConfig() {
      clearInterval(SI);
      doSave(JSON.stringify(buildSavePayload()), 0);
    }

    function updateStatus() {
      fetch('/api/status')
        .then(r => r.json())
        .then(d => {
          const m = document.getElementById('mqttStatus');
          if (!m) return;
          if (d.mqttConnected) {
            m.innerHTML = '<span style="color:#34d399">MQTT OK</span> - ' + d.onlineCount + ' online';
          } else {
            m.innerHTML = '<span style="color:#f87171">MQTT Disconnected</span>';
          }
        })
        .catch(() => {});
    }

    loadConfig();
    updateStatus();
    SI = setInterval(updateStatus, 5000);
  </script>
</body>
</html>
)rawliteral";

// Pre-calculated length to avoid slow strlen_P() on every chunk
const size_t HTML_MONITOR_LEN = sizeof(HTML_MONITOR) - 1;

#endif
