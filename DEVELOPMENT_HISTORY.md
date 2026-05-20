# worki 專案開發紀錄 (Development History)

這份文件記錄了使用 AI 輔助開發 worki 專案的過程、關鍵決策與技術細節。

---

## 2026-05-18: 專案初始化

### 任務內容
- 初始化 Git 存儲庫。
- 設定專案開發規範 `GEMINI.md`。
- 建立開發紀錄檔 `DEVELOPMENT_HISTORY.md`。

### 關鍵決策
- 確立「嵌入式開發專家」的角色設定。
- 採用繁體中文進行溝通，專業術語保留英文。
- 規範每次對話後需更新此紀錄檔以利後續追蹤。

### 專案狀態
- [x] 環境初始化完成。
- [ ] 待定義 worki 專案具體功能需求。

---

## 2026-05-20: STM32 Blue Pill Wokwi 單檔功能實作

### 任務內容
- 依 `readme.md` 實作單一 `sketch.ino`，供 Wokwi STM32 Blue Pill 網頁環境直接測試。
- 功能包含 DS1307 RTC、DHT22、LCD2004 I2C backpack、Serial 同步輸出。
- 依 Bottom-Up 原則在同一檔案內保留 `SELF_TEST_MODE`，可切換 Tier 1 bus、Tier 2 device、Tier 3 scheduler 測試。

### 關鍵決策
- 不使用 `Wire`、`LiquidCrystal`、`DHT` 或 `String`，自行實作 bit-bang I2C 與 DHT22 single-wire protocol。
- I2C SDA/SCL 使用 open-drain 模式概念：輸出低電位或切回 `INPUT_PULLUP` 釋放匯流排。
- I2C clock stretch、bus recovery、DHT edge wait 皆具備 `micros()` based hard timeout，避免死鎖。
- DHT22 溫溼度採 fixed-point `x10` 整數保存與格式化，避免浮點格式化。
- Serial/LCD 每 1000ms 輸出，RTC 每 800ms 讀取，DHT22 每 5000ms 讀取；排程使用 `lastTask = now` 避免追趕死循環。
- LCD 使用 HD44780 degree code `0xDF`；Serial 使用 UTF-8 degree symbol bytes。

### 驗證方式
- 已執行 `cpp -DPA0=0 -DPB6=1 -DPB7=2 sketch.ino` 確認 preprocessor path 可通過。
- 已執行 `git diff --check` 檢查 whitespace。
- 本地環境無 `arduino-cli`、`platformio`、`gcc/g++/clang`，最終編譯與模擬需於 Wokwi 網頁環境執行。

### 專案狀態
- [x] `sketch.ino` 單檔實作完成。
- [x] Tier 1/Tier 2/Tier 3 測試模式已內建。
- [ ] 待使用者於 Wokwi 網頁執行實機模擬確認波形與周邊顯示。
