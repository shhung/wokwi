# worki 專案開發紀錄 (Development History)

這份文件記錄了使用 AI 輔助開發 worki 專案的過程、關鍵決策與技術細節。

---

## 2026-05-18: 專案初始化與架構設計

### 使用者需求 (Prompt)
- 實作 worki 專案並記錄 AI 開發過程，建立 Git 追蹤。
- 角色定位：嵌入式開發專家。
- 維護一份文件以快速瀏覽開發過程。

### 任務內容
- 初始化 Git 存儲庫。
- 設定專案開發規範 `GEMINI.md`。
- 建立開發紀錄檔 `DEVELOPMENT_HISTORY.md`。

### 關鍵決策
- 確立「嵌入式開發專家」的角色設定。
- 採用繁體中文進行溝通，專業術語保留英文。
- 規範每次對話後需更新此紀錄檔以利後續追蹤。

---

## 2026-05-18: 技術架構與開發規範調整

### 使用者需求 (Prompt)
- 分析 `readme.md` 需求並提出討論。
- 1. 先以 bare-metal 方式實作，Serial、LOG 及基礎系統功能可使用 Arduino API。
- 2. 可以操控 I2C 控制器。
- 3. 用簡單的 Super loop，盡量精簡程式。
- 4. 電路圖上看起來並沒有轉接板（針對 LCD2004）。
- 5. 程式碼撰寫在 `sketch.ino`。
- 6. 每次對話完做一次 commit 紀錄。
- 7. 針對提出的 prompt 也做紀錄。

### 關鍵決策
- **實作層級**：底層驅動（I2C, 1-Wire, LCD, RTC）採用 **Bare-metal** 直接操作暫存器的方式。
- **輔助工具**：`Serial` 與基礎系統功能沿用 **Arduino API**。
- **任務排程**：使用 **Super Loop** 搭配 `millis()` 進行非阻塞定時。
- **LCD 驅動**：因位址為 `0x27`，推斷需實作 PCF8574 I2C 轉接板邏輯。

---

## 2026-05-18: 程式架構與資料結構設計

### 使用者需求 (Prompt)
- 目前已有基本 Serial 輸出，確認 Super loop 運作正常。
- 在接續開發前，要求說明更詳細的程式架構，包含預計如何分層、設計怎樣的資料結構。

### 關鍵決策與架構規劃
**1. 軟體分層設計 (Layering)**
為維持 Bare-metal 的極簡性同時保持可維護性，程式將分為三層：
- **硬體抽象層 (HAL/Bus Layer)**:
    - 負責最底層的時序與暫存器操作。
    - `i2c_start()`, `i2c_write()`, `i2c_read()`
    - `onewire_reset()`, `onewire_write_bit()`, `onewire_read_bit()`
- **裝置驅動層 (Device Driver Layer)**:
    - 利用 Bus Layer 提供的 API 與具體感測器/顯示器溝通。
    - `ds1307_read_time()`
    - `pcf8574_lcd_write_cmd()`, `pcf8574_lcd_print()`
    - `dht22_read_data()`
- **應用邏輯層 (Application Layer)**:
    - `setup()` 與 `loop()` 所在，負責任務排程 (1000ms/5000ms)、狀態更新與畫面刷新邏輯。

**2. 資料結構設計 (Data Structures)**
使用 `struct` 來統一管理設備狀態，避免全域變數散落。
- **RTC 結構**: `struct RtcTime { uint8_t year, month, day, hour, min, sec; bool ok; };`
- **DHT 結構**: `struct DhtData { float temp; float humd; bool ok; };`

---

## 2026-05-18: 實作 I2C Bus Layer 與資料結構

### 使用者需求 (Prompt)
- 可以（開始實作）。

### 關鍵決策
- **I2C 核心實作**：採用 Polling 方式操作 I2C 暫存器。實作了 `i2c_start`, `i2c_stop`, `i2c_write`, `i2c_read` 及 `i2c_send_addr`。
- **錯誤處理**：在等待 Flag 時加入簡易的 timeout 機制以防止匯流排異常導致當機。
- **資料結構**：在 `sketch.ino` 中正式定義了 `RtcTime` 與 `DhtData` 結構。

### 技術細節
- **ADDR Flag 清除**：STM32 I2C 控制器的 ADDR flag 需要讀取 SR1 後再讀取 SR2 才能清除，已在 `i2c_send_addr` 中實作。
- **ACK/NACK 控制**：在讀取函式中加入 ACK 控制，以便在讀取多個 Byte 或最後一個 Byte 時正確操作。

### 專案狀態
- [x] 完成軟體架構與資料結構規劃。
- [x] 實作 I2C Bus Layer 與 DS1307 驅動。
- [x] 實作 DHT22 (1-Wire) 驅動。
- [x] 整合 Serial 輸出格式化邏輯。
- [ ] 待修復 LCD 顯示問題。

---

## 2026-05-18: 實作 DHT22 驅動與系統整合

### 使用者需求 (Prompt)
- 目前 RTC 與 Serial 輸出正常，但 LCD 依舊無畫面。
- 建議先將 DHT 打通。

### 關鍵決策
- **DHT22 (1-Wire) 實作**：使用 PA0 腳位。由於 STM32F103 無硬體 1-Wire，採用 **Bit-banging** 方式。
    - 實作了 Start Signal (18ms Low)。
    - 使用精確的 `delayMicroseconds` 捕捉 DHT22 的 40-bit 響應。
    - 加入 Checksum 校驗邏輯。
- **LCD 佈局優化**：為了代碼精簡，將 `lcd_set_cursor` 與 `lcd_print` 邏輯內聯化到 `loop()` 中，減少函式呼叫。
- **初始化延遲**：將 `setup` 的初始延遲增加到 500ms，確保所有外設上電穩定。

---

## 2026-05-18: 診斷與修復 I2C 匯流排掛起問題

### 使用者需求 (Prompt)
- 回報系統在初始化後僅輸出一次 LOG，且 LCD 無顯示，代表程式掛起。

### 關鍵決策
- **掛起原因分析**：STM32 Hardware I2C 在模擬器中若遇到未回應的位址（如 LCD 尚未就緒時發送位址），可能會導致 `SR1` 的 Flag 永遠不會被設定，造成 `i2c_wait_flag` 陷入無限死循環。
- **修復方案**：
    1. **Timeout 保護**：在 `i2c_wait_flag` 中加入遞減計數與 `delayMicroseconds`，確保即使硬體未回應，程式也能跳出迴圈。
    2. **初始化延遲**：在 `setup` 與 `i2c_init` 中增加適當延遲，確保外設穩定。
    3. **I2C 軟重置**：在 `i2c_init` 中增加 `SWRST` 控制位元，強制重設 I2C 控制器狀態。
- **LCD 優化**：稍微放寬 `lcd_pulse` 的延遲，確保 LCD 控制器有足夠時間鎖定資料。

---

## 2026-05-18: 修復 LCD 驅動整合錯誤

### 使用者需求 (Prompt)
- 回報 LCD 相關函式未宣告以及 Super Loop 代碼損壞。

### 關鍵決策
- **損壞原因**：在使用 `replace` 工具進行局部替換時，未能正確定位區塊，導致舊代碼殘留並產生語法錯誤。
- **修復方案**：使用 `write_file` 重新構建完整的 `sketch.ino`。將 LCD 驅動（PCF8574 + HD44780 4-bit）完整嵌入，並確保所有函式宣告順序正確。
- **UI 調整**：在 `loop()` 中確保 `lcd_init` 被正確呼叫，並移除所有損壞的 `llis();` 等殘留片段。

---

## 2026-05-18: 實作 LCD2004 (PCF8574) 驅動與畫面排版

### 使用者需求 (Prompt)
- 目前 Serial 輸出符合預期（2000/00/00 00:00:00 為 RTC 未設定時的預期行為）。
- 繼續開發。

### 關鍵決策
- **LCD 控制策略**：採用 PCF8574 I2C 轉接板進行 HD44780 控制。實作了 **4-bit 傳輸模式** 以符合轉接板的接線結構。
- **畫面佈局**：根據 `readme.md` 要求實作了 4 行顯示邏輯，包含格式化時間、溫濕度佔位符與模組連線狀態。
- **字元處理**：HD44780 的度數符號 `⁰C` 使用對應的字模碼 `0xDF` 處理。

### 技術細節
- **PCF8574 映射**：定義了 P0-P2 為 RS/RW/EN，P4-P7 為 Data Line 的對應關係。
- **初始化序列**：嚴格遵循 HD44780 的 4-bit 初始化時序（0x03, 0x03, 0x03, 0x02），確保在模擬器中穩定啟動。
- **非阻塞更新**：LCD 刷新邏輯整合於 1000ms 的任務中。

---

## 2026-05-18: 修復編譯錯誤與程式碼重構

### 使用者需求 (Prompt)
- 回報 build failed 並提供詳細錯誤訊息。

### 關鍵決策
- **原因分析**：
    1. **宣告順序錯誤**：`ds1307_read_time` 與 `print_time` 在 `loop()` 之後才定義，導致編譯器找不到符號。
    2. **工具產生殘留**：`replace` 工具在多次編輯同一個區塊時產生了損壞的代碼片段（如 `d")`）。
- **修復方案**：重新整理 `sketch.ino` 的結構，將所有函式定義移至 `setup()` 與 `loop()` 之前，並直接以 `write_file` 覆寫以確保乾淨的代碼狀態。

### 技術細節
- 統一了程式碼的分層順序：Register -> Struct -> Global Var -> Bus Layer -> Device Driver -> Helper -> Arduino Framework (setup/loop)。

---

## 2026-05-18: 實作 DS1307 RTC 驅動與基礎測試

### 使用者需求 (Prompt)
- 根據專業判斷決定下一步，如果已可以測試就先測試。

### 關鍵決策
- **優先順序**：選擇先實作 **DS1307 RTC** 驅動。因為 I2C 匯流排已就緒，且 RTC 是系統時間基準，優先完成有利於後續整體功能的時戳記錄。
- **測試策略**：在 `loop()` 中加入 `ds1307_read_time` 呼叫，並透過 `Serial` 輸出格式化時間，以驗證 I2C 讀寫與 BCD 轉換邏輯。

### 技術細節
- **BCD 轉換**：實作了 `bcd2dec` 函式，處理 DS1307 暫存器中的 BCD 格式數據。
- **時序驗證**：在 `ds1307_read_time` 中正確使用了 Repeated Start 以符合 DS1307 的讀取協議。
- **輸出格式**：初步實作了符合需求格式的 Serial 輸出邏輯（包含異常處理佔位）。
