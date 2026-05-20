# AI coding 

需求如下:
在wokwi 模擬的stm32 blue pill上使用C語言開發功能，
內容包含:

- 每1000ms以內(可小於)讀取 RTC DS1307
- 每5000ms 讀取一次溫溼度數據 DHT22
- 每1000ms 同步更新 serial 及 LCD2004 內容
- 須自行實做 bus driver（I2C、1-Wire）及 device driver

 LCD 輸出格式
```bash
# 正常時
Line 1:  HH:MM:SS  YYYY/MM/DD
Line 2:  Temp: XXX.X⁰C
Line 3:  Humd: XXX.X%
Line 4:  RTC: OK   DHT: OK
# LCD 異常時
# RTC異常
# 時間：--:--:--
# 日期：----/--/--
# 狀態：RTC: ERR
Line 1:  --:--:--  ----/--/--
Line 2:  Temp: XXX.X⁰C
Line 3:  Humd: XXX.X%
Line 4:  RTC: ERR   DHT: OK
# DHT22異常
# 溫度：---.-
# 濕度：---.-
# 狀態：DHT: ERR
Line 1:  HH:MM:SS  YYYY/MM/DD
Line 2:  Temp: ---.-⁰C
Line 3:  Humd: ---.-%
Line 4:  RTC: OK   DHT: ERR
```
狀態列即時反映模組連線狀態

```bash
# Serial 輸出格式
# 正常
YYYY/MM/DD HH:MM:SS | XXX.X⁰C | XXX.X%
# 讀取失敗的裝置對應數字均顯示「-」
----/--/-- --:--:-- | XXX.X⁰C | 45.9%
```

周邊配置如下  

| Func | Pin |
|-|--|
| DHT22 | A0 |
| I2C SDA | B7 |
| I2C SCL | B6 |
| Serial TX | A9 |
| Serial RX | A10 |

| Peri | Addr |
|-|--|
| LCD (LCD2004) | Addr 0x27 |
| RTC (DS1307) | Addr 0x68 |
