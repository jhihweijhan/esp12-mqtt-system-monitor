# ESP12 MQTT 穩定性修復報告（2026-02-22）

## 背景與目標

- 問題：ESP12 可連上 MQTT broker，但裝置端畫面常出現 `OFFLINE`、偶發重啟，且無法穩定顯示 sender 指標。
- 本次目標：
  - 讓 MQTT 訊息可穩定進入顯示流程。
  - 降低斷線後無法自癒的風險。
  - 針對 malformed payload 導致的崩潰路徑進行防護。
  - 每次韌體更新後，進行至少 60 秒觀察，並每 10 秒採樣一次畫面。

## 事件與根因分析

### 1) 訂閱空集合造成無資料死循環

- 現象：`subscribedTopics` 為空時，韌體不訂閱 sender topic，畫面停在 waiting。
- 根因：allowlist 模式下，未選 topic 會直接不訂閱，且 UI 初期可能尚未發現 topic。
- 修復：
  - 當 `subscribedTopics` 為空時，fallback 訂閱 legacy `mqttTopic`。
  - fallback 模式收到訊息時允許自動啟用對應 device。

### 2) 畫面誤判 `OFFLINE`

- 現象：序列埠有 `MQTT rx`，但畫面仍常顯示 `OFFLINE`。
- 根因：離線判斷使用較早的 `now`，在同一 loop 內若 `lastUpdate` 更新較晚，可能出現 unsigned underflow，誤判逾時。
- 修復：
  - 新增安全時間比較 helper，統一使用 signed delta 判斷。
  - 離線判斷改用當下時間點 `offlineCheckNow`。

### 3) 偶發 `MQTT payload rejected (invalid length)` 與重啟

- 現象：序列埠偶發出現異常長度（例如 `42949xxxx`）後，出現 exception reset。
- 根因：接收到 malformed publish frame 時，PubSubClient 在 topic/payload 邊界上缺乏足夠防護，可能走到危險記憶體路徑。
- 修復：
  - 透過 PlatformIO pre-script 自動 patch PubSubClient。
  - 在 publish 解析處新增邊界檢查；一旦 frame 不合法，立即 stop 連線並丟棄該包，避免越界/underflow 影響後續流程。

## 實作摘要

### A. MQTT 訂閱與自癒策略

- 新增 policy：
  - `shouldFallbackToLegacyTopicSubscription(...)`
  - `shouldAutoEnableDeviceOnTopicMessage(...)`
  - `shouldResubscribeWhenStreamStalled(...)`
- 效果：
  - 避免零訂閱。
  - fallback 時可快速把新 sender 帶進顯示。
  - 保留 stalled 時 re-subscribe 能力。

### B. 離線判斷防誤判

- 新增 `hasElapsedIntervalMs(...)`，取代不安全的直接 unsigned 減法比較。
- 離線判斷改在實際檢查點取 `millis()`，減少時間競態造成的誤判。

### C. PubSubClient 解析防護

- 新增 `scripts/patch_pubsubclient.py`，在 build 前 patch `.pio/libdeps/.../PubSubClient.cpp`。
- patch 內容重點：
  - 在 `MQTTPUBLISH` 回呼前，先驗證 topic 與 payload 邊界是否合法。
  - 檢測到不合法 frame 時，直接 `_client->stop()` 並回傳，避免繼續處理。
- 優點：
  - 不需要 fork 整個依賴庫。
  - build 可重現，並可檢測是否已 patch。

### D. 其他穩定性調整

- 物件生命週期由 dynamic allocation 改為靜態實例，減少 heap 碎片與啟動流程不確定性。
- MQTT 解析改為 filter parse，降低 JSON 熱路徑負載。
- 資源上限調整為 minimal profile（device/topic upper bound 下修）。
- 預設上傳序列埠改為實際環境 `ttyUSB0`。

## 測試與驗證

### 自動化驗證

- `pio test -e native`：16/16 通過（含新增 policy 測試）。
- `pio run`：編譯成功。
- `pio run -t upload`：上傳成功（/dev/ttyUSB0）。

### 裝置觀察流程

- 每次更新後執行：
  - 序列埠連續擷取約 75 秒。
  - webcam 每 10 秒擷取一次，共 0/10/20/30/40/50/60 秒。

### 最後一輪觀察（patch 後）

- 序列埠重點：
  - 持續出現 `MQTT connected`、`Subscribed sender topic`、`MQTT rx: ...`。
  - 本輪未觀察到 `MQTT payload rejected (invalid length)` + exception reset 的組合。
- 畫面採樣：
  - 多數時間為指標頁，底部顯示 `MQTT OK`。
  - 少量瞬時畫面切換（輪播/切頁）屬預期，未見長時間卡死在 `OFFLINE`。

## 目前狀態與結論

- 已解決「常態 OFFLINE 誤判」的主因。
- 已封堵 malformed MQTT frame 造成的高風險崩潰路徑（透過 PubSubClient 邊界檢查 patch）。
- 目前 MQTT 接收與畫面顯示已達可運行狀態，重連後可恢復顯示。

## 最新補充（仍觀察到 offline 的原因）

- 使用者回報仍偶爾看到 `OFFLINE`，因此追加調整：
  - 新增 `shouldMarkDeviceOffline(...)` policy，加入 reconnect 後短暫 grace（預設 12 秒）。
  - 避免 MQTT 剛重連時立即把 device 標記 offline。
- 追加測試：
  - `test_device_offline_decision_policy`（native 測試通過）。

### 追加觀察結果

- 一輪觀察（obs9）顯示 WiFi 連線連續超時，設備停留在 `Connecting NightHawk-2.4G`，未進入 MQTT 階段。
- 再一輪觀察（obs10）同樣是 WiFi timeout 循環（`/wifi.json` 與 SDK 憑證都失敗）。

### 判讀

- 在這兩輪中，`OFFLINE` 的直接主因不是 MQTT parser，而是 WiFi 無法穩定連上 AP，導致上層無法維持資料流。
- 因此目前剩餘的不穩定性已從「MQTT 解析崩潰」轉為「WiFi 連線品質/可用性」。

## 後續建議

1. broker 端清理可疑 retained 訊息並確認 publisher 名單，降低 malformed frame 來源風險。
2. 若要長期維護，建議把 PubSubClient patch 上游化（fork 或提交 patch），避免依賴 `.pio` 路徑結構。
3. 增加「disconnect reason / reconnect reason」統計欄位，持續量化 `<= 5 秒恢復` 達標率。
