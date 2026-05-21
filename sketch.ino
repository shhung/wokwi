/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * VERSION: STABLE_FSM_V2
 * FIX: Interspaced FSM, Hardware I2C, and RTC Initialization.
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

// --- Hardware I2C1 Register Definitions ---
#define I2C1_BASE      (0x40005400)
#define I2C1_CR1       (*((volatile uint32_t *)(I2C1_BASE + 0x00)))
#define I2C1_CR2       (*((volatile uint32_t *)(I2C1_BASE + 0x04)))
#define I2C1_DR        (*((volatile uint32_t *)(I2C1_BASE + 0x10)))
#define I2C1_SR1       (*((volatile uint32_t *)(I2C1_BASE + 0x14)))
#define I2C1_SR2       (*((volatile uint32_t *)(I2C1_BASE + 0x18)))
#define I2C1_CCR       (*((volatile uint32_t *)(I2C1_BASE + 0x1C)))
#define I2C1_TRISE     (*((volatile uint32_t *)(I2C1_BASE + 0x20)))
#define RCC_APB1ENR    (*((volatile uint32_t *)0x4002101C))

// --- I2C Bus Layer (Hardware Controller) ---
bool i2c_wait_flag(uint32_t reg, uint32_t flag, bool set) {
    uint32_t timeout = 20000;
    if (set) while (!(*((volatile uint32_t *)reg) & flag)) { if (--timeout == 0) return false; }
    else while (*((volatile uint32_t *)reg) & flag) { if (--timeout == 0) return false; }
    return true;
}

void i2c_init() {
    RCC_APB2ENR |= (1 << 3); 
    RCC_APB1ENR |= (1 << 21);
    GPIOB_CRL &= ~(0xFF000000);
    GPIOB_CRL |= (0xEE000000); // PB6, PB7 AF-OD
    I2C1_CR1 |= (1 << 15); I2C1_CR1 &= ~(1 << 15);
    I2C1_CR2 = 8; I2C1_CCR = 40; I2C1_TRISE = 9;
    I2C1_CR1 |= (1 << 10) | (1 << 0); // Enable ACK and I2C Peripheral
}

bool i2c_start() {
    I2C1_CR1 |= (1 << 8);
    return i2c_wait_flag(I2C1_BASE + 0x14, (1 << 0), true);
}

void i2c_stop() { I2C1_CR1 |= (1 << 9); }

bool i2c_send_addr(uint8_t addr) {
    I2C1_DR = addr;
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 1), true)) return false;
    (void)I2C1_SR1; (void)I2C1_SR2;
    return true;
}

bool i2c_write(uint8_t data) {
    I2C1_DR = data;
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 7), true)) return false;
    if (I2C1_SR1 & (1 << 10)) { I2C1_SR1 &= ~(1 << 10); return false; }
    return true;
}

uint8_t i2c_read(bool ack, bool stop) {
    if (!ack) I2C1_CR1 &= ~(1 << 10); // Clear ACK before receiving last byte
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 6), true)) return 0;
    if (stop) i2c_stop(); // Generate STOP before reading DR
    uint8_t data = (uint8_t)I2C1_DR;
    if (!ack) I2C1_CR1 |= (1 << 10); // Restore ACK
    return data;
}

// --- LCD Driver ---
#define PIN_RS (1<<0)
#define PIN_EN (1<<2)
#define PIN_BL (1<<3)

void pcf_write(uint8_t d) { 
    if (i2c_start()) {
        if (i2c_send_addr(lcd_addr << 1)) i2c_write(d | PIN_BL); 
        i2c_stop(); 
    }
    delayMicroseconds(50);
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
uint8_t d2b(uint8_t v) { return ((v / 10 * 16) + (v % 10)); }

void ds1307_read(RtcTime* t) {
    t->ok = false;
    if (!i2c_start()) { i2c_stop(); return; }
    if (!i2c_send_addr(0x68 << 1)) { i2c_stop(); return; }
    if (!i2c_write(0x00)) { i2c_stop(); return; }
    i2c_stop(); delayMicroseconds(100);

    if (!i2c_start()) { i2c_stop(); return; }
    if (!i2c_send_addr((0x68 << 1) | 1)) { i2c_stop(); return; }
    
    uint8_t r[7];
    for (int i = 0; i < 6; i++) r[i] = i2c_read(true, false);
    r[6] = i2c_read(false, true); // NACK and STOP
    
    t->sec = b2d(r[0] & 0x7F); t->min = b2d(r[1]); t->hour = b2d(r[2] & 0x3F);
    t->day = b2d(r[4]); t->month = b2d(r[5]); t->year = b2d(r[6]);
    t->ok = (t->month > 0 && t->month <= 12 && t->day > 0 && t->day <= 31);
}

void ds1307_init() {
    RtcTime t;
    ds1307_read(&t);
    if (!t.ok) { // CH bit set or invalid date detected
        if (i2c_start()) {
            if (i2c_send_addr(0x68 << 1)) {
                i2c_write(0x00); i2c_write(0x00); // Start clock
                i2c_write(0x00); i2c_write(0x12); // Min, Hour
                i2c_write(0x01); i2c_write(0x21); // Day, Month
                i2c_write(0x26); // Year 2026
                i2c_stop();
            }
        }
    }
}

// --- DHT Driver ---
void dht_set_out() { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= 0x3; }
void dht_set_in()  { GPIOA_CRL &= ~(0xF); GPIOA_CRL |= 0x4; }
bool dht_read(DhtData* d) {
    d->ok = false;
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
    return false;
}

void setup() {
    Serial.begin(115200); delay(1000);
    i2c_init();
    ds1307_init();
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
            if (current_time.ok) {
                sprintf(fsm_buf, "%04d/%02d/%02d %02d:%02d:%02d | ", 2000 + current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
            } else {
                sprintf(fsm_buf, "----/--/-- --:--:-- | ");
            }
            Serial.print(fsm_buf);
            if (current_dht.ok) {
                sprintf(fsm_buf, "%d.%d\xC2\xB0\x43 | %d.%d%%", current_dht.temp10 / 10, abs(current_dht.temp10 % 10), current_dht.humd10 / 10, abs(current_dht.humd10 % 10));
            } else {
                sprintf(fsm_buf, "- | -");
            }
            Serial.println(fsm_buf);
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
            last1sTask = now; 
            fsm_step = 1;
            lastFsmStep = now;
        } else if (now - last5sTask >= 5000) {
            last5sTask = now;
            fsm_step = 6;
            lastFsmStep = now;
        }
    } else {
        if (now - lastFsmStep >= 100) {
            lastFsmStep = now;
            run_fsm();
        }
    }
}
