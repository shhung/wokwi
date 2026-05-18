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
- [x] 確定開發策略：Bare-metal 實作（底層），Arduino API（Serial/LOG/系統基礎）。
- [ ] 待實作 I2C Bus Driver。
- [ ] 待實作 1-Wire Bus Driver (DHT22)。
- [ ] 待實作 LCD2004 驅動與 RTC 讀取。

## 2026-05-18: 確定技術架構與需求澄清

### 關鍵決策
- **實作層級**：底層驅動（I2C, 1-Wire, LCD, RTC）採用 **Bare-metal** 直接操作暫存器的方式，追求極簡與高效。
- **輔助工具**：`Serial` 與基礎系統功能（如 `delay()`, `millis()`）沿用 **Arduino API** 以加速 LOG 輸出與開發。
- **任務排程**：使用 **Super Loop** 搭配 `millis()` 進行非阻塞定時，避免使用 RTOS。
- **開發環境**：以 **Wokwi (Web)** 為主，主要程式碼集中於 `sketch.ino`。

### 技術疑點與分析
- **LCD2004 I2C 位址 (0x27)**：雖然使用者提及電路圖未見轉接板，但 0x27 是典型的 PCF8574 轉接板位址。實作時將針對 PCF8574 + HD44780 組合進行驅動開發。
- **STM32 Hardware I2C**：將直接配置 STM32F103 的暫存器（CR1, CR2, OAR1, DR, SR1, SR2）來實現 I2C 通訊。

