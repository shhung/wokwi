/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * FIX: Hybrid I2C Driver for Simulator Compatibility.
 * BASE: LCD_STABLE (c92e1e7)
 */

#include <Arduino.h>

// --- Register Definitions ---
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40021018))
#define GPIOA_CRL      (*((volatile uint32_t *)0x40010800))
#define GPIOA_ODR      (*((volatile uint32_t *)0x4001080C))
#define GPIOA_IDR      (*((volatile uint32_t *)0x40010808))
#define GPIOB_CRL      (*((volatile uint32_t *)0x40010C00))
#define GPIOB_IDR      (*((volatile uint32_t *)0x40010C08))
#define GPIOB_ODR      (*((volatile uint32_t *)0x40010C0C))
#define GPIOB_BSRR     (*((volatile uint32_t *)0x40010C10))
#define GPIOB_BRR      (*((volatile uint32_t *)0x40010C14))

// --- Data Structures ---
struct RtcTime { uint8_t year, month, day, hour, min, sec; bool ok; };
struct DhtData { float temp, humd; bool ok; };

// --- Global Variables ---
RtcTime current_time = {0, 0, 0, 0, 0, 0, false};
DhtData current_dht = {0.0f, 0.0f, false};
unsigned long lastRtcLcdUpdate = 0;
unsigned long lastDhtUpdate = 0;
uint8_t lcd_addr = 0x27;
char lcd_buf[4][21] = {"", "", "", ""};

// --- I2C Bus Layer (Hybrid Drive Mode) ---
#define SCL_PIN 6
#define SDA_PIN 7

#define I2C_DELAY() delayMicroseconds(50)

void sda_high() { 
    GPIOB_CRL &= ~(0xF0000000); 
    GPIOB_CRL |=  (0x80000000); // Input with Pull
    GPIOB_ODR |=  (1 << SDA_PIN); 
}
void sda_low() { 
    GPIOB_CRL &= ~(0xF0000000); 
    GPIOB_CRL |=  (0x30000000); // Output Push-Pull
    GPIOB_ODR &= ~(1 << SDA_PIN); 
}
void scl_high() { 
    GPIOB_CRL &= ~(0x0F000000); 
    GPIOB_CRL |=  (0x08000000); // Input with Pull
    GPIOB_ODR |=  (1 << SCL_PIN); 
}
void scl_low() { 
    GPIOB_CRL &= ~(0x0F000000); 
    GPIOB_CRL |=  (0x03000000); // Output Push-Pull
    GPIOB_ODR &= ~(1 << SCL_PIN); 
}
bool sda_read() { return (GPIOB_IDR & (1 << SDA_PIN)); }

void i2c_init() {
    RCC_APB2ENR |= (1 << 3) | (1 << 2); 
    sda_high(); scl_high(); delay(100);
}

void i2c_start() {
    sda_high(); scl_high(); I2C_DELAY();
    sda_low();  I2C_DELAY();
    scl_low();  I2C_DELAY();
}

void i2c_stop() {
    sda_low();  I2C_DELAY();
    scl_high(); I2C_DELAY();
    sda_high(); I2C_DELAY();
}

bool i2c_write(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        if (data & 0x80) sda_high(); else sda_low();
        data <<= 1; I2C_DELAY();
        scl_high(); I2C_DELAY();
        scl_low();  I2C_DELAY();
    }
    sda_high(); I2C_DELAY();
    scl_high(); I2C_DELAY();
    bool ack = !sda_read();
    scl_low();  I2C_DELAY();
    return ack;
}

uint8_t i2c_read(bool ack) {
    uint8_t data = 0;
    sda_high(); I2C_DELAY();
    for (int i = 0; i < 8; i++) {
        scl_high(); I2C_DELAY();
        I2C_DELAY(); 
        if (sda_read()) data |= (1 << (7 - i));
        scl_low();  I2C_DELAY();
    }
    if (ack) sda_low(); else sda_high();
    I2C_DELAY();
    scl_high(); I2C_DELAY();
    scl_low();  I2C_DELAY();
    sda_high(); I2C_DELAY();
    return data;
}

// --- DHT22 Driver ---
void dht_set_output() { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= (1 << 1) | (1 << 0); }
void dht_set_input()  { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= (1 << 2); }

bool dht_read_data(DhtData* data) {
    uint8_t b[5] = {0};
    dht_set_output(); GPIOA_ODR &= ~(1 << 0); delay(20); GPIOA_ODR |= (1 << 0); delayMicroseconds(40); dht_set_input();
    uint32_t t = 10000;
    while ((GPIOA_IDR & (1 << 0)) && --t); if (!t) return false;
    t = 10000; while (!(GPIOA_IDR & (1 << 0)) && --t); if (!t) return false;
    t = 10000; while ((GPIOA_IDR & (1 << 0)) && --t); if (!t) return false;
    for (int i = 0; i < 40; i++) {
        t = 10000; while (!(GPIOA_IDR & (1 << 0)) && --t);
        delayMicroseconds(35);
        if (GPIOA_IDR & (1 << 0)) {
            b[i/8] |= (1 << (7 - (i%8)));
            t = 10000; while ((GPIOA_IDR & (1 << 0)) && --t);
        }
    }
    if (b[4] == ((b[0]+b[1]+b[2]+b[3]) & 0xFF)) {
        int16_t h = (b[0] << 8) | b[1];
        int16_t temp = ((b[2] & 0x7F) << 8) | b[3];
        if (b[2] & 0x80) temp *= -1;
        data->humd = h / 10.0; data->temp = temp / 10.0; data->ok = true;
        return true;
    }
    data->ok = false; return false;
}

// --- LCD Driver (STABLE VERSION) ---
#define PIN_RS (1<<0)
#define PIN_RW (1<<1)
#define PIN_EN (1<<2)
#define PIN_BL (1<<3)

void pcf_write(uint8_t d) { 
    i2c_start(); 
    if (i2c_write(lcd_addr << 1)) i2c_write(d | PIN_BL); 
    i2c_stop(); 
    delayMicroseconds(100); 
}

void lcd_pulse(uint8_t d) {
    pcf_write(d | PIN_EN); delayMicroseconds(50);
    pcf_write(d & ~PIN_EN); delayMicroseconds(50);
}

void lcd_send_4(uint8_t n, uint8_t m) {
    uint8_t d = (n << 4) | m;
    pcf_write(d); lcd_pulse(d);
}

void lcd_send(uint8_t v, uint8_t m)   { lcd_send_4(v>>4, m); lcd_send_4(v&0x0F, m); }
void lcd_cmd(uint8_t c) { lcd_send(c, 0); delayMicroseconds(100); }
void lcd_dat(uint8_t d) { lcd_send(d, PIN_RS); }

void lcd_init() {
    delay(200);
    lcd_send_4(0x03, 0); delay(10); lcd_send_4(0x03, 0); delay(5); lcd_send_4(0x03, 0); delay(5);
    lcd_send_4(0x02, 0); delay(5);
    lcd_cmd(0x28); lcd_cmd(0x0C); lcd_cmd(0x06); lcd_cmd(0x01); delay(10);
}

void lcd_write_line(int row, const char* txt) {
    if (strcmp(lcd_buf[row], txt) == 0) return;
    strncpy(lcd_buf[row], txt, 20);
    lcd_buf[row][20] = '\0';
    uint8_t pos[] = {0x00, 0x40, 0x14, 0x54};
    lcd_cmd(0x80 | pos[row]);
    bool reached_end = false;
    for (int i = 0; i < 20; i++) {
        if (!reached_end && txt[i] == '\0') reached_end = true;
        if (!reached_end) lcd_dat(txt[i]); else lcd_dat(' ');
    }
}

// --- RTC Driver ---
uint8_t b2d(uint8_t v) { return ((v / 16 * 10) + (v % 16)); }
void ds1307_init() {
    i2c_start();
    if (i2c_write(0x68 << 1)) {
        i2c_write(0x00); 
        i2c_stop(); delayMicroseconds(100); i2c_start(); 
        i2c_write((0x68 << 1) | 1);
        uint8_t s = i2c_read(false); i2c_stop();
        if (s & 0x80) {
            i2c_start(); i2c_write(0x68 << 1); i2c_write(0x00); 
            i2c_write(0x00); i2c_write(0x00); i2c_write(0x12); 
            i2c_write(0x01); i2c_write(0x19); i2c_write(0x05); i2c_write(0x26); 
            i2c_stop();
        }
    }
}

void ds1307_read(RtcTime* t) {
    i2c_start();
    if (!i2c_write(0x68 << 1)) { i2c_stop(); t->ok = false; return; }
    i2c_write(0x00); 
    i2c_stop(); delayMicroseconds(50); i2c_start(); 
    if (!i2c_write((0x68 << 1) | 1)) { i2c_stop(); t->ok = false; return; }
    uint8_t raw[7];
    for(int i=0; i<6; i++) raw[i] = i2c_read(true);
    raw[6] = i2c_read(false);
    i2c_stop();
    t->sec=b2d(raw[0]&0x7F); t->min=b2d(raw[1]); t->hour=b2d(raw[2]&0x3F);
    t->day=b2d(raw[4]); t->month=b2d(raw[5]); t->year=b2d(raw[6]);
    if (t->month == 0 || t->month > 12 || t->day == 0 || t->day > 31) {
        t->ok = false;
    } else {
        t->ok = true;
    }
}

void setup() {
    Serial.begin(115200); delay(1000);
    i2c_init();
    lcd_addr = 0x27;
    i2c_start();
    if (!i2c_write(0x27 << 1)) { i2c_stop(); i2c_start(); if (i2c_write(0x3F << 1)) lcd_addr = 0x3F; }
    i2c_stop();
    ds1307_init();
    lcd_init();
    Serial.println("System Ready");
}

void loop() {
    unsigned long now = millis();
    if (now - lastRtcLcdUpdate >= 1000) {
        lastRtcLcdUpdate = now;
        ds1307_read(&current_time);
        
        // Serial log
        if (current_time.ok) {
            char buf[32];
            sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d", 2000+current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
            Serial.print(buf);
        } else {
            Serial.print("----/--/-- --:--:--");
        }
        Serial.print(" | ");
        if (current_dht.ok) { 
            Serial.print(current_dht.temp, 1); Serial.print("\xC2\xB0\x43 | "); 
            Serial.print(current_dht.humd, 1); Serial.println("%"); 
        } else { 
            Serial.println("- | -"); 
        }

        // LCD layout
        char line[21];
        if (current_time.ok)
            sprintf(line, "%02d:%02d:%02d  %04d/%02d/%02d", current_time.hour, current_time.min, current_time.sec, 2000+current_time.year, current_time.month, current_time.day);
        else
            sprintf(line, "--:--:--  ----/--/--");
        lcd_write_line(0, line);
        
        if (current_dht.ok) {
            String t_str = String(current_dht.temp, 1);
            sprintf(line, "Temp: %s\xDF\x43", t_str.c_str());
        } else {
            sprintf(line, "Temp: ---.-\xDF\x43");
        }
        lcd_write_line(1, line);

        if (current_dht.ok) {
            String h_str = String(current_dht.humd, 1);
            sprintf(line, "Humd: %s%%", h_str.c_str());
        } else {
            sprintf(line, "Humd: ---.-%%");
        }
        lcd_write_line(2, line);

        sprintf(line, "RTC: %-4s  DHT: %-4s", current_time.ok?"OK":"ERR", current_dht.ok?"OK":"ERR");
        lcd_write_line(3, line);
    }
    if (now - lastDhtUpdate >= 5000) {
        lastDhtUpdate = now;
        dht_read_data(&current_dht);
    }
}
