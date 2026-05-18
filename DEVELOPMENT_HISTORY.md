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

### 專案狀態
- [x] 環境初始化完成。
- [x] 確定開發策略。
- [ ] 待實作 I2C Bus Driver (Start/Stop/Write/Read)。
- [ ] 待實作 1-Wire Bus Driver (DHT22)。
- [ ] 待實作 LCD2004 驅動與 RTC 讀取。
*LCD2004 I2C 位址 (0x27)**：雖然使用者提及電路圖未見轉接板，但 0x27 是典型的 PCF8574 轉接板位址。實作時將針對 PCF8574 + HD44780 組合進行驅動開發。
- **STM32 Hardware I2C**：將直接配置 STM32F103 的暫存器（CR1, CR2, OAR1, DR, SR1, SR2）來實現 I2C 通訊。

