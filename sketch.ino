/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * Features:
 * - Robust Bit-Banging I2C (SCL: PB6, SDA: PB7)
 * - Custom 1-Wire DHT22 (PA0)
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

// --- I2C Bus Layer (Enhanced Bit-Banging) ---
#define SCL_PIN 6
#define SDA_PIN 7

// Use slower delay for maximum compatibility with simulator
#define I2C_DELAY() delayMicroseconds(5)

void sda_high() { GPIOB_BSRR = (1 << SDA_PIN); }
void sda_low()  { GPIOB_BRR  = (1 << SDA_PIN); }
void scl_high() { GPIOB_BSRR = (1 << SCL_PIN); }
void scl_low()  { GPIOB_BRR  = (1 << SCL_PIN); }
bool sda_read() { return (GPIOB_IDR & (1 << SDA_PIN)); }

void i2c_init() {
    RCC_APB2ENR |= (1 << 3) | (1 << 2); 
    // PB6, PB7: Open-Drain Output, 2MHz (CNF=01, MODE=10 is 0x6 but 0x5 is 10MHz, 0x5 is fine)
    GPIOB_CRL &= ~(0xFF000000);
    GPIOB_CRL |=  (0x55000000); 
    sda_high(); scl_high();
}

void i2c_start() {
    sda_high(); scl_high(); I2C_DELAY();
    sda_low();  I2C_DELAY();
    scl_low();  I2C_DELAY();
}

void i2c_stop() {
    sda_low();  scl_low();  I2C_DELAY();
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
    sda_high(); // Release for ACK
    scl_high(); I2C_DELAY();
    bool ack = !sda_read();
    scl_low();  I2C_DELAY();
    return ack;
}

uint8_t i2c_read(bool ack) {
    uint8_t data = 0;
    sda_high(); // Ensure SDA is released
    for (int i = 0; i < 8; i++) {
        scl_high(); I2C_DELAY();
        data <<= 1;
        if (sda_read()) data |= 1;
        scl_low();  I2C_DELAY();
    }
    if (ack) sda_low(); else sda_high();
    scl_high(); I2C_DELAY();
    scl_low();  I2C_DELAY();
    sda_high();
    return data;
}

// --- DHT22 Driver ---
void dht_set_output() { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= (1 << 1) | (1 << 0); }
void dht_set_input()  { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= (1 << 2); }

bool dht_read_data(DhtData* data) {
    uint8_t b[5] = {0};
    dht_set_output(); GPIOA_ODR &= ~(1 << 0); delay(20); GPIOA_ODR |= (1 << 0); delayMicroseconds(35); dht_set_input();
    uint32_t t = 1000;
    while ((GPIOA_IDR & (1 << 0)) && --t); if (!t) return false;
    t = 1000; while (!(GPIOA_IDR & (1 << 0)) && --t); if (!t) return false;
    t = 1000; while ((GPIOA_IDR & (1 << 0)) && --t); if (!t) return false;
    for (int i = 0; i < 40; i++) {
        t = 1000; while (!(GPIOA_IDR & (1 << 0)) && --t);
        delayMicroseconds(40);
        if (GPIOA_IDR & (1 << 0)) {
            b[i/8] |= (1 << (7 - (i%8)));
            t = 1000; while ((GPIOA_IDR & (1 << 0)) && --t);
        }
    }
    if (b[4] == ((b[0]+b[1]+b[2]+b[3]) & 0xFF)) {
        int16_t h = (b[0] << 8) | b[1];
        int16_t temp = ((b[2] & 0x7F) << 8) | b[3];
        if (b[2] & 0x80) temp *= -1;
        data->humd = h / 10.0; data->temp = temp / 10.0; data->ok = true;
        return true;
    }
    return false;
}

// --- LCD Driver ---
#define PIN_RS (1<<0)
#define PIN_EN (1<<2)
#define PIN_BL (1<<3)

void pcf_write(uint8_t d) { i2c_start(); if (i2c_write(lcd_addr << 1)) i2c_write(d | PIN_BL); i2c_stop(); }
void lcd_pulse(uint8_t d) { pcf_write(d|PIN_EN); delayMicroseconds(5); pcf_write(d&~PIN_EN); delayMicroseconds(50); }
void lcd_send_4(uint8_t n, uint8_t m) { uint8_t d=(n<<4)|m; pcf_write(d); lcd_pulse(d); }
void lcd_send(uint8_t v, uint8_t m)   { lcd_send_4(v>>4, m); lcd_send_4(v&0x0F, m); }
void lcd_cmd(uint8_t c) { lcd_send(c, 0); }
void lcd_dat(uint8_t d) { lcd_send(d, PIN_RS); }

void lcd_init() {
    delay(100);
    lcd_send_4(0x03, 0); delay(5); lcd_send_4(0x03, 0); delay(1); lcd_send_4(0x03, 0); lcd_send_4(0x02, 0);
    lcd_cmd(0x28); lcd_cmd(0x0C); lcd_cmd(0x06); lcd_cmd(0x01); delay(5);
}

// --- RTC Driver ---
uint8_t b2d(uint8_t v) { return ((v / 16 * 10) + (v % 16)); }
void ds1307_init() {
    i2c_start();
    if (i2c_write(0x68 << 1)) {
        i2c_write(0x00); i2c_start(); i2c_write((0x68 << 1) | 1);
        uint8_t s = i2c_read(false); i2c_stop();
        if (s & 0x80) { i2c_start(); i2c_write(0x68 << 1); i2c_write(0x00); i2c_write(0x00); i2c_stop(); }
    }
}
void ds1307_read(RtcTime* t) {
    i2c_start();
    if (!i2c_write(0x68 << 1)) { i2c_stop(); t->ok = false; return; }
    i2c_write(0x00); i2c_start(); i2c_write((0x68 << 1) | 1);
    t->sec=b2d(i2c_read(true)&0x7F); t->min=b2d(i2c_read(true)); t->hour=b2d(i2c_read(true)&0x3F);
    (void)i2c_read(true); t->day=b2d(i2c_read(true)); t->month=b2d(i2c_read(true)); t->year=b2d(i2c_read(false));
    i2c_stop(); t->ok = true;
}

void setup() {
    Serial.begin(115200); delay(500);
    i2c_init();
    // Scan for LCD Address
    if (i2c_write(0x27 << 1)) lcd_addr = 0x27;
    else if (i2c_write(0x3F << 1)) lcd_addr = 0x3F;
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
        char buf[64];
        if (current_time.ok) {
            sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d", 2000+current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
            Serial.print(buf);
        } else Serial.print("RTC: ERR");
        Serial.print(" | ");
        if (current_dht.ok) { Serial.print(current_dht.temp, 1); Serial.print("C | "); Serial.print(current_dht.humd, 1); Serial.println("%"); }
        else Serial.println("DHT: ERR");

        // LCD layout
        char l[4][21];
        sprintf(l[0], "%02d:%02d:%02d  %04d/%02d/%02d", current_time.hour, current_time.min, current_time.sec, 2000+current_time.year, current_time.month, current_time.day);
        sprintf(l[1], "Temp: %s C", current_dht.ok ? String(current_dht.temp, 1).c_str() : "---.-");
        sprintf(l[2], "Humd: %s %%", current_dht.ok ? String(current_dht.humd, 1).c_str() : "---.-");
        sprintf(l[3], "RTC:%s DHT:%s", current_time.ok?"OK":"ERR", current_dht.ok?"OK":"ERR");
        
        uint8_t r_off[] = {0x00, 0x40, 0x14, 0x54};
        for(int i=0; i<4; i++) {
            lcd_cmd(0x80 | r_off[i]);
            const char* p = l[i]; while(*p) lcd_dat(*p++);
        }
    }
    if (now - lastDhtUpdate >= 5000) {
        lastDhtUpdate = now;
        dht_read_data(&current_dht);
    }
}
