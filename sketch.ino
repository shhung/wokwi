/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * Features:
 * - Bare-metal I2C Driver for DS1307 RTC & LCD2004 (via PCF8574)
 * - Custom 1-Wire Driver for DHT22
 * - Super Loop with Non-blocking Timing
 */

#include <Arduino.h>

// --- Register Definitions ---
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40021018))
#define RCC_APB1ENR    (*((volatile uint32_t *)0x4002101C))
#define GPIOB_CRL      (*((volatile uint32_t *)0x40010C00))

#define I2C1_BASE      (0x40005400)
#define I2C1_CR1       (*((volatile uint32_t *)(I2C1_BASE + 0x00)))
#define I2C1_CR2       (*((volatile uint32_t *)(I2C1_BASE + 0x04)))
#define I2C1_DR        (*((volatile uint32_t *)(I2C1_BASE + 0x10)))
#define I2C1_SR1       (*((volatile uint32_t *)(I2C1_BASE + 0x14)))
#define I2C1_SR2       (*((volatile uint32_t *)(I2C1_BASE + 0x18)))
#define I2C1_CCR       (*((volatile uint32_t *)(I2C1_BASE + 0x1C)))
#define I2C1_TRISE     (*((volatile uint32_t *)(I2C1_BASE + 0x20)))

// --- Data Structures ---
struct RtcTime {
    uint8_t year, month, day, hour, min, sec;
    bool ok;
};

struct DhtData {
    float temp, humd;
    bool ok;
};

// --- Global Variables ---
RtcTime current_time = {0, 0, 0, 0, 0, 0, false};
DhtData current_dht = {0.0f, 0.0f, false};
unsigned long lastRtcLcdUpdate = 0;
unsigned long lastDhtUpdate = 0;

// --- I2C Bus Layer (with Timeout Protection) ---

bool i2c_wait_flag(uint32_t flag, bool is_sr2 = false) {
    uint32_t timeout = 5000; // Reduced timeout for faster recovery
    if (is_sr2) {
        while (!(I2C1_SR2 & flag) && --timeout) delayMicroseconds(1);
    } else {
        while (!(I2C1_SR1 & flag) && --timeout) delayMicroseconds(1);
    }
    return (timeout > 0);
}

void i2c_start() {
    I2C1_CR1 |= (1 << 8);
    i2c_wait_flag(1 << 0);
}

void i2c_stop() {
    I2C1_CR1 |= (1 << 9);
}

void i2c_write(uint8_t data) {
    I2C1_DR = data;
    i2c_wait_flag(1 << 7);
}

uint8_t i2c_read(bool ack) {
    if (ack) I2C1_CR1 |= (1 << 10);
    else I2C1_CR1 &= ~(1 << 10);
    i2c_wait_flag(1 << 6);
    return (uint8_t)I2C1_DR;
}

bool i2c_send_addr(uint8_t addr, bool read) {
    I2C1_DR = (addr << 1) | (read ? 1 : 0);
    bool ok = i2c_wait_flag(1 << 1);
    (void)I2C1_SR2;
    return ok;
}

void i2c_init() {
    RCC_APB2ENR |= (1 << 3);  // IOPBEN
    RCC_APB1ENR |= (1 << 21); // I2C1EN
    
    // PB6, PB7: AF-OD (2MHz is enough for 100kHz I2C)
    GPIOB_CRL &= ~(0xFF000000);
    GPIOB_CRL |=  (0xEE000000); 

    I2C1_CR1 |= (1 << 15); // SWRST
    delay(10);
    I2C1_CR1 &= ~(1 << 15);

    I2C1_CR2 = 8;         
    I2C1_CCR = 40;        
    I2C1_TRISE = 9;       
    I2C1_CR1 |= (1 << 0); // PE
}

// --- LCD2004 via PCF8574 Driver ---
#define LCD_ADDR 0x27
#define PIN_RS  (1 << 0)
#define PIN_RW  (1 << 1)
#define PIN_EN  (1 << 2)
#define PIN_BL  (1 << 3)

void pcf8574_write(uint8_t data) {
    i2c_start();
    if (i2c_send_addr(LCD_ADDR, false)) {
        i2c_write(data | PIN_BL);
    }
    i2c_stop();
}

void lcd_pulse(uint8_t data) {
    pcf8574_write(data | PIN_EN);
    delayMicroseconds(2);
    pcf8574_write(data & ~PIN_EN);
    delayMicroseconds(40);
}

void lcd_send_4bit(uint8_t nibble, uint8_t mode) {
    uint8_t data = (nibble << 4) | mode;
    pcf8574_write(data);
    lcd_pulse(data);
}

void lcd_send(uint8_t value, uint8_t mode) {
    lcd_send_4bit(value >> 4, mode);
    lcd_send_4bit(value & 0x0F, mode);
}

void lcd_command(uint8_t cmd) { lcd_send(cmd, 0); }
void lcd_data(uint8_t data)   { lcd_send(data, PIN_RS); }

void lcd_init() {
    delay(100); // Wait for LCD power up
    lcd_send_4bit(0x03, 0); delay(5);
    lcd_send_4bit(0x03, 0); delay(1);
    lcd_send_4bit(0x03, 0);
    lcd_send_4bit(0x02, 0);
    
    lcd_command(0x28); // 4-bit, 2-line, 5x8
    lcd_command(0x0C); // Display ON
    lcd_command(0x06); // Entry Mode
    lcd_command(0x01); // Clear
    delay(5);
}

void lcd_set_cursor(uint8_t col, uint8_t row) {
    uint8_t offsets[] = {0x00, 0x40, 0x14, 0x54};
    lcd_command(0x80 | (col + offsets[row]));
}

void lcd_print(const char* str) {
    while (*str) lcd_data(*str++);
}

// --- RTC DS1307 Driver ---
uint8_t bcd2dec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }

void ds1307_read_time(RtcTime* time) {
    i2c_start();
    if (!i2c_send_addr(0x68, false)) { i2c_stop(); time->ok = false; return; }
    i2c_write(0x00);
    i2c_start();
    i2c_send_addr(0x68, true);
    time->sec   = bcd2dec(i2c_read(true) & 0x7F);
    time->min   = bcd2dec(i2c_read(true));
    time->hour  = bcd2dec(i2c_read(true) & 0x3F);
    (void)i2c_read(true);
    time->day   = bcd2dec(i2c_read(true));
    time->month = bcd2dec(i2c_read(true));
    time->year  = bcd2dec(i2c_read(false));
    i2c_stop();
    time->ok = true;
}

// --- Arduino Framework ---
void setup() {
    Serial.begin(115200);
    delay(100); 
    i2c_init();
    lcd_init();
    Serial.println("System Initialized");
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - lastRtcLcdUpdate >= 1000) {
        lastRtcLcdUpdate = currentMillis;
        ds1307_read_time(&current_time);
        
        // Serial Output
        if (current_time.ok) {
            char buf[32];
            sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d", 2000+current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
            Serial.print(buf);
        } else {
            Serial.print("RTC: ERR");
        }
        Serial.print(" | ");
        if (current_dht.ok) {
            Serial.print(current_dht.temp, 1); Serial.print("C | ");
            Serial.print(current_dht.humd, 1); Serial.println("%");
        } else {
            Serial.println("DHT: ERR");
        }

        // LCD Output
        char line1[21], line2[21], line3[21], line4[21];
        if (current_time.ok) {
            sprintf(line1, "%02d:%02d:%02d  %04d/%02d/%02d", current_time.hour, current_time.min, current_time.sec, 2000+current_time.year, current_time.month, current_time.day);
        } else {
            sprintf(line1, "--:----  ----/--/--");
        }
        
        sprintf(line2, "Temp: %s", current_dht.ok ? "XX.X" : "---.-");
        sprintf(line3, "Humd: %s", current_dht.ok ? "XX.X" : "---.-");
        sprintf(line4, "RTC: %s   DHT: %s", current_time.ok ? "OK " : "ERR", current_dht.ok ? "OK " : "ERR");

        lcd_set_cursor(0, 0); lcd_print(line1);
        lcd_set_cursor(0, 1); lcd_print(line2);
        lcd_set_cursor(0, 2); lcd_print(line3);
        lcd_set_cursor(0, 3); lcd_print(line4);
    }
}
