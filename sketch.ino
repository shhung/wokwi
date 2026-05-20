/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * VERSION: STABLE_FSM_V2
 * FIX: Interspaced FSM, Conservative Timings, and Buffer Safety.
 */

#include <Arduino.h>

// --- Register Definitions ---
#define GPIOB_CRL      (*((volatile uint32_t *)0x40010C00))
#define GPIOB_IDR      (*((volatile uint32_t *)0x40010C08))
#define GPIOB_ODR      (*((volatile uint32_t *)0x40010C0C))
#define GPIOA_CRL      (*((volatile uint32_t *)0x40010800))
#define GPIOA_ODR      (*((volatile uint32_t *)0x4001080C))
#define GPIOA_IDR      (*((volatile uint32_t *)0x40010808))
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40021018))

// --- Data Structures ---
struct RtcTime { uint8_t year, month, day, hour, min, sec; bool ok; };
struct DhtData { int16_t temp10, humd10; bool ok; };

// --- Global Variables ---
RtcTime current_time = {0, 0, 0, 0, 0, 0, false};
DhtData current_dht = {0, 0, false};
unsigned long last1sTask = 0;
unsigned long last5sTask = 0;
unsigned long lastFsmStep = 0;
uint8_t lcd_addr = 0x27;
char lcd_buf[4][21] = {"", "", "", ""};
int fsm_step = 0;

// --- I2C Bus Layer (Conservative 50us) ---
#define SCL_PIN 6
#define SDA_PIN 7
#define I2C_DELAY() delayMicroseconds(50)

void sda_high() { 
    GPIOB_CRL &= ~(0xF0000000); GPIOB_CRL |= (0x80000000); 
    GPIOB_ODR |= (1 << SDA_PIN); 
}
void sda_low() { 
    GPIOB_CRL &= ~(0xF0000000); GPIOB_CRL |= (0x30000000); 
    GPIOB_ODR &= ~(1 << SDA_PIN); 
}
void scl_high() { 
    GPIOB_CRL &= ~(0x0F000000); GPIOB_CRL |= (0x08000000); 
    GPIOB_ODR |= (1 << SCL_PIN); 
}
void scl_low() { 
    GPIOB_CRL &= ~(0x0F000000); GPIOB_CRL |= (0x03000000); 
    GPIOB_ODR &= ~(1 << SCL_PIN); 
}
bool sda_read() { return (GPIOB_IDR & (1 << SDA_PIN)); }

void i2c_recover() {
    sda_high();
    for (int i = 0; i < 10; i++) {
        scl_low(); I2C_DELAY(); scl_high(); I2C_DELAY();
        if (sda_read()) break;
    }
    sda_low(); I2C_DELAY(); scl_high(); I2C_DELAY(); sda_high(); I2C_DELAY();
}

void i2c_init() {
    RCC_APB2ENR |= (1 << 3) | (1 << 2); 
    i2c_recover();
}

void i2c_start() {
    sda_high(); scl_high(); I2C_DELAY();
    sda_low(); I2C_DELAY();
    scl_low(); I2C_DELAY();
}

void i2c_stop() {
    sda_low(); I2C_DELAY();
    scl_high(); I2C_DELAY();
    sda_high(); I2C_DELAY();
}

bool i2c_write(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        if (data & 0x80) sda_high(); else sda_low();
        data <<= 1; I2C_DELAY(); scl_high(); I2C_DELAY(); scl_low(); I2C_DELAY();
    }
    sda_high(); I2C_DELAY(); scl_high(); I2C_DELAY();
    bool ack = !sda_read();
    scl_low(); I2C_DELAY();
    return ack;
}

uint8_t i2c_read(bool ack) {
    uint8_t data = 0;
    sda_high(); I2C_DELAY();
    for (int i = 0; i < 8; i++) {
        scl_high(); I2C_DELAY();
        if (sda_read()) data |= (1 << (7 - i));
        scl_low(); I2C_DELAY();
    }
    if (ack) sda_low(); else sda_high();
    I2C_DELAY(); scl_high(); I2C_DELAY(); scl_low(); I2C_DELAY();
    sda_high(); I2C_DELAY();
    return data;
}

// --- LCD Driver ---
#define PIN_RS (1<<0)
#define PIN_EN (1<<2)
#define PIN_BL (1<<3)

void pcf_write(uint8_t d) { 
    i2c_start(); 
    if (i2c_write(lcd_addr << 1)) i2c_write(d | PIN_BL); 
    i2c_stop(); 
    delayMicroseconds(100);
}

void lcd_pulse(uint8_t d) {
    pcf_write(d | PIN_EN); delayMicroseconds(150);
    pcf_write(d & ~PIN_EN); delayMicroseconds(150);
}

void lcd_send_4(uint8_t n, uint8_t m) {
    uint8_t d = (n << 4) | m;
    pcf_write(d); lcd_pulse(d);
}

void lcd_send(uint8_t v, uint8_t m) { lcd_send_4(v >> 4, m); lcd_send_4(v & 0x0F, m); }
void lcd_cmd(uint8_t c) { lcd_send(c, 0); delayMicroseconds(200); }
void lcd_dat(uint8_t d) { lcd_send(d, PIN_RS); }

void lcd_init() {
    delay(500);
    lcd_send_4(0x03, 0); delay(10); lcd_send_4(0x03, 0); delay(5); lcd_send_4(0x03, 0); delay(5);
    lcd_send_4(0x02, 0); delay(5);
    lcd_cmd(0x28); lcd_cmd(0x0C); lcd_cmd(0x06); lcd_cmd(0x01); delay(20);
}

void lcd_write_line(int row, const char* txt) {
    if (strcmp(lcd_buf[row], txt) == 0) return;
    strncpy(lcd_buf[row], txt, 20); lcd_buf[row][20] = '\0';
    uint8_t pos[] = {0x00, 0x40, 0x14, 0x54};
    lcd_cmd(0x80 | pos[row]);
    bool padding = false;
    for (int i = 0; i < 20; i++) {
        if (!padding && txt[i] == '\0') padding = true;
        lcd_dat(padding ? ' ' : txt[i]);
    }
}

// --- RTC Driver ---
uint8_t b2d(uint8_t v) { return ((v / 16 * 10) + (v % 16)); }
void ds1307_read(RtcTime* t) {
    i2c_start();
    if (!i2c_write(0x68 << 1)) { i2c_stop(); t->ok = false; return; }
    i2c_write(0x00); i2c_stop(); delayMicroseconds(100); i2c_start();
    if (!i2c_write((0x68 << 1) | 1)) { i2c_stop(); t->ok = false; return; }
    uint8_t r[7];
    for (int i = 0; i < 6; i++) r[i] = i2c_read(true);
    r[6] = i2c_read(false); i2c_stop();
    t->sec = b2d(r[0] & 0x7F); t->min = b2d(r[1]); t->hour = b2d(r[2] & 0x3F);
    t->day = b2d(r[4]); t->month = b2d(r[5]); t->year = b2d(r[6]);
    t->ok = (t->month > 0 && t->month <= 12 && t->day > 0 && t->day <= 31);
}

// --- DHT Driver ---
void dht_set_out() { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= 0x3; }
void dht_set_in()  { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= 0x4; }
bool dht_read(DhtData* d) {
    uint8_t b[5] = {0};
    dht_set_out(); GPIOA_ODR &= ~1; delay(20); GPIOA_ODR |= 1; delayMicroseconds(40); dht_set_in();
    
    unsigned long start = micros();
    while ((GPIOA_IDR & 1)) if (micros() - start > 100) return false;
    start = micros();
    while (!(GPIOA_IDR & 1)) if (micros() - start > 100) return false;
    start = micros();
    while ((GPIOA_IDR & 1)) if (micros() - start > 100) return false;

    for (int i = 0; i < 40; i++) {
        start = micros();
        while (!(GPIOA_IDR & 1)) if (micros() - start > 100) return false;
        delayMicroseconds(35);
        if (GPIOA_IDR & 1) {
            b[i / 8] |= (1 << (7 - (i % 8)));
            start = micros();
            while ((GPIOA_IDR & 1)) if (micros() - start > 100) return false;
        }
    }
    if (b[4] == ((b[0] + b[1] + b[2] + b[3]) & 0xFF)) {
        d->humd10 = ((b[0] << 8) | b[1]);
        int16_t temp = ((b[2] & 0x7F) << 8) | b[3];
        if (b[2] & 0x80) temp = -temp;
        d->temp10 = temp;
        d->ok = true; return true;
    }
    d->ok = false; return false;
}

void setup() {
    Serial.begin(115200); delay(1000);
    i2c_init();
    lcd_init();
    last1sTask = millis();
    last5sTask = millis();
    Serial.println("System Ready");
}

void run_fsm() {
    static char fsm_buf[64];
    switch (fsm_step) {
        case 1: // Task: Read RTC & Serial Log
            ds1307_read(&current_time);
            if (current_time.ok && current_dht.ok) {
                sprintf(fsm_buf, "%04d/%02d/%02d %02d:%02d:%02d | %d.%d C | %d.%d%%", 2000 + current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec, current_dht.temp10 / 10, abs(current_dht.temp10 % 10), current_dht.humd10 / 10, abs(current_dht.humd10 % 10));
                Serial.println(fsm_buf);
            } else if (current_time.ok) {
                sprintf(fsm_buf, "%04d/%02d/%02d %02d:%02d:%02d | - | -", 2000 + current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
                Serial.println(fsm_buf);
            } else {
                Serial.println("----/--/-- --:--:-- | - | -");
            }
            fsm_step++;
            break;
        case 2: // LCD Line 0
            if (current_time.ok) sprintf(fsm_buf, "%02d:%02d:%02d  %04d/%02d/%02d", current_time.hour, current_time.min, current_time.sec, 2000 + current_time.year, current_time.month, current_time.day);
            else sprintf(fsm_buf, "--:--:--  ----/--/--");
            lcd_write_line(0, fsm_buf);
            fsm_step++;
            break;
        case 3: // LCD Line 1
            if (current_dht.ok) sprintf(fsm_buf, "Temp: %d.%d\xDF\x43", current_dht.temp10 / 10, abs(current_dht.temp10 % 10));
            else sprintf(fsm_buf, "Temp: ---.-\xDF\x43");
            lcd_write_line(1, fsm_buf);
            fsm_step++;
            break;
        case 4: // LCD Line 2
            if (current_dht.ok) sprintf(fsm_buf, "Humd: %d.%d%%", current_dht.humd10 / 10, abs(current_dht.humd10 % 10));
            else sprintf(fsm_buf, "Humd: ---.-%%");
            lcd_write_line(2, fsm_buf);
            fsm_step++;
            break;
        case 5: // LCD Line 3
            sprintf(fsm_buf, "RTC: %-4s  DHT: %-4s", current_time.ok ? "OK" : "ERR", current_dht.ok ? "OK" : "ERR");
            lcd_write_line(3, fsm_buf);
            fsm_step = 0;
            break;
        case 6: // Task: Read DHT
            dht_read(&current_dht);
            fsm_step = 0;
            break;
    }
}

void loop() {
    unsigned long now = millis();
    
    if (fsm_step == 0) {
        if (now - last1sTask >= 1000) {
            last1sTask += 1000;
            if (now - last1sTask > 1000) last1sTask = now; // Prevent catch-up spiral
            fsm_step = 1;
            lastFsmStep = now;
        } else if (now - last5sTask >= 5000) {
            last5sTask += 5000;
            if (now - last5sTask > 5000) last5sTask = now;
            fsm_step = 6;
            lastFsmStep = now;
        }
    } else {
        // Execute one state every 100ms to spread the I2C load
        if (now - lastFsmStep >= 100) {
            lastFsmStep = now;
            run_fsm();
        }
    }
}
