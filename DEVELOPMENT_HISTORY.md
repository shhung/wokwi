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
- [x] 環境初始化完成。
- [x] 確定開發策略與架構。
- [x] 實作 I2C Bus Layer (Start/Stop/Write/Read/Addr)。
- [ ] 待實作 1-Wire Bus Driver (DHT22)。
- [ ] 待實作 DS1307 RTC 讀取驅動。
- [ ] 待實作 LCD2004 (PCF8574) 驅動。
