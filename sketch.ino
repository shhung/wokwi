/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * VERSION: RTC_DRIVEN_LOOP_V3
 * FIX: Stable baseline for production.
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
unsigned long lastRtcPoll = 0;
unsigned long lastFallbackUpdate = 0;
unsigned long lastDhtMillis = 0;
uint8_t lastRtcSecond = 0xFF;
uint32_t lastDhtRtcSecond = 0;
bool haveDhtRtcSecond = false;
uint8_t lcd_addr = 0x27;
char lcd_buf[4][21] = {"", "", "", ""};

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

void i2c_recover() {
    I2C1_CR1 |= (1 << 15);
    delayMicroseconds(10);
    I2C1_CR1 &= ~(1 << 15);
    I2C1_CR2 = 8; I2C1_CCR = 40; I2C1_TRISE = 9;
    I2C1_CR1 |= (1 << 10) | (1 << 0);
}

bool i2c_start() {
    if (!i2c_wait_flag(I2C1_BASE + 0x18, (1 << 1), false)) {
        i2c_recover();
        if (!i2c_wait_flag(I2C1_BASE + 0x18, (1 << 1), false)) return false;
    }
    I2C1_CR1 |= (1 << 8);
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 0), true)) {
        i2c_recover();
        return false;
    }
    return true;
}

void i2c_stop() { I2C1_CR1 |= (1 << 9); }

bool i2c_send_addr(uint8_t addr) {
    I2C1_DR = addr;
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 1), true)) {
        if (I2C1_SR1 & (1 << 10)) I2C1_SR1 &= ~(1 << 10);
        i2c_recover();
        return false;
    }
    (void)I2C1_SR1; (void)I2C1_SR2;
    return true;
}

bool i2c_write(uint8_t data) {
    I2C1_DR = data;
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 7), true)) {
        i2c_recover();
        return false;
    }
    if (I2C1_SR1 & (1 << 10)) {
        I2C1_SR1 &= ~(1 << 10);
        i2c_recover();
        return false;
    }
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 2), true)) {
        i2c_recover();
        return false;
    }
    return true;
}

bool i2c_read(uint8_t* data, bool ack, bool stop) {
    if (!ack) I2C1_CR1 &= ~(1 << 10); // Clear ACK before receiving last byte
    if (!i2c_wait_flag(I2C1_BASE + 0x14, (1 << 6), true)) {
        if (stop) i2c_stop();
        I2C1_CR1 |= (1 << 10);
        i2c_recover();
        return false;
    }
    if (stop) i2c_stop(); // Generate STOP before reading DR
    *data = (uint8_t)I2C1_DR;
    if (!ack) I2C1_CR1 |= (1 << 10); // Restore ACK
    return true;
}

// --- LCD Driver ---
#define PIN_RS (1<<0)
#define PIN_EN (1<<2)
#define PIN_BL (1<<3)

void pcf_write(uint8_t d) { 
    bool ok = false;

    if (i2c_start()) {
        if (i2c_send_addr(lcd_addr << 1)) ok = i2c_write(d | PIN_BL);
        i2c_stop(); 
    }
    if (!ok) {
        i2c_recover();
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
    char next[21];
    int i = 0;

    for (; i < 20 && txt[i] != '\0'; i++) next[i] = txt[i];
    for (; i < 20; i++) next[i] = ' ';
    next[20] = '\0';

    if (strncmp(lcd_buf[row], next, 20) == 0) return;

    uint8_t pos[] = {0x00, 0x40, 0x14, 0x54};
    i = 0;
    while (i < 20) {
        if (lcd_buf[row][i] == next[i]) {
            i++;
            continue;
        }

        lcd_cmd(0x80 | (pos[row] + i));
        while (i < 20 && lcd_buf[row][i] != next[i]) {
            lcd_dat(next[i]);
            lcd_buf[row][i] = next[i];
            i++;
        }
    }
}

// --- RTC Driver ---
uint8_t b2d(uint8_t v) { return ((v / 16 * 10) + (v % 16)); }
uint8_t d2b(uint8_t v) { return ((v / 10 * 16) + (v % 10)); }
bool valid_bcd(uint8_t v) { return ((v & 0x0F) <= 9 && ((v >> 4) & 0x0F) <= 9); }

void ds1307_read(RtcTime* t) {
    t->ok = false;
    if (!i2c_start()) { i2c_stop(); return; }
    if (!i2c_send_addr(0x68 << 1)) { i2c_stop(); return; }
    if (!i2c_write(0x00)) { i2c_stop(); return; }
    i2c_stop(); delayMicroseconds(100);

    if (!i2c_start()) { i2c_stop(); return; }
    if (!i2c_send_addr((0x68 << 1) | 1)) { i2c_stop(); return; }
    
    uint8_t r[7];
    for (int i = 0; i < 6; i++) {
        if (!i2c_read(&r[i], true, false)) { i2c_stop(); return; }
    }
    if (!i2c_read(&r[6], false, true)) { i2c_stop(); return; } // NACK and STOP

    bool clock_halted = (r[0] & 0x80) != 0;
    bool hour_12h = (r[2] & 0x40) != 0;
    uint8_t raw_sec = r[0] & 0x7F;
    uint8_t raw_min = r[1] & 0x7F;
    uint8_t raw_hour = r[2] & 0x3F;
    uint8_t raw_day = r[4] & 0x3F;
    uint8_t raw_month = r[5] & 0x1F;

    t->sec = b2d(raw_sec); t->min = b2d(raw_min); t->hour = b2d(raw_hour);
    t->day = b2d(raw_day); t->month = b2d(raw_month); t->year = b2d(r[6]);
    t->ok = (!clock_halted && !hour_12h &&
        valid_bcd(raw_sec) && t->sec <= 59 &&
        valid_bcd(raw_min) && t->min <= 59 &&
        valid_bcd(raw_hour) && t->hour <= 23 &&
        valid_bcd(raw_day) && t->day > 0 && t->day <= 31 &&
        valid_bcd(raw_month) && t->month > 0 && t->month <= 12 &&
        valid_bcd(r[6]));
}

void ds1307_init() {
    RtcTime t;
    ds1307_read(&t);
    if (!t.ok) { // CH bit set or invalid date detected
        if (i2c_start()) {
            if (i2c_send_addr(0x68 << 1)) {
                i2c_write(0x00); i2c_write(0x00); // Start clock
                i2c_write(0x00); i2c_write(0x12); // Min, Hour
                i2c_write(0x01); i2c_write(0x21); // Day-of-week, Date
                i2c_write(0x05); // Month
                i2c_write(0x26); // Year 2026
                i2c_stop();
            }
        }
    }
}

// --- DHT Driver ---
void dht_drive_low() {
    GPIOA_ODR &= ~1;
    GPIOA_CRL &= ~(0xF);
    GPIOA_CRL |= 0x6; // PA0 open-drain output, 2 MHz
}

void dht_release() {
    GPIOA_ODR |= 1;
    GPIOA_CRL &= ~(0xF);
    GPIOA_CRL |= 0x8; // PA0 input with pull-up
}

bool dht_read(DhtData* d) {
    d->ok = false;
    uint8_t b[5] = {0};
    dht_drive_low(); delay(20); dht_release(); delayMicroseconds(40);
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

uint32_t rtc_second_of_day(const RtcTime* t) {
    return ((uint32_t)t->hour * 3600UL) + ((uint32_t)t->min * 60UL) + t->sec;
}

uint32_t rtc_elapsed_seconds(uint32_t now, uint32_t last) {
    return (now + 86400UL - last) % 86400UL;
}

void put_2d(char* p, uint8_t v) {
    p[0] = '0' + (v / 10);
    p[1] = '0' + (v % 10);
}

void put_4d(char* p, uint16_t v) {
    p[0] = '0' + ((v / 1000) % 10);
    p[1] = '0' + ((v / 100) % 10);
    p[2] = '0' + ((v / 10) % 10);
    p[3] = '0' + (v % 10);
}

void append_char(char** p, char* end, char c) {
    if (*p < end) *(*p)++ = c;
}

void append_text(char** p, char* end, const char* s) {
    while (*s) append_char(p, end, *s++);
}

void append_uint(char** p, char* end, uint16_t v) {
    char tmp[5];
    uint8_t n = 0;

    do {
        tmp[n++] = '0' + (v % 10);
        v /= 10;
    } while (v && n < sizeof(tmp));

    while (n > 0) append_char(p, end, tmp[--n]);
}

void append_tenth(char** p, char* end, int16_t v) {
    if (v < 0) {
        append_char(p, end, '-');
        v = -v;
    }
    append_uint(p, end, v / 10);
    append_char(p, end, '.');
    append_char(p, end, '0' + (v % 10));
}

void format_lcd_time_line(char* buf, size_t size) {
    if (size < 21) return;

    if (!current_time.ok) {
        memcpy(buf, "--:--:--  ----/--/--", 21);
        return;
    }

    memcpy(buf, "00:00:00  0000/00/00", 21);
    put_2d(&buf[0], current_time.hour);
    put_2d(&buf[3], current_time.min);
    put_2d(&buf[6], current_time.sec);
    put_4d(&buf[10], 2000 + current_time.year);
    put_2d(&buf[15], current_time.month);
    put_2d(&buf[18], current_time.day);
}

void format_serial_line(char* line, size_t size) {
    if (size == 0) return;

    char* p = line;
    char* end = line + size - 1;

    if (current_time.ok) {
        char t[20];
        memcpy(t, "0000/00/00 00:00:00", 20);
        put_4d(&t[0], 2000 + current_time.year);
        put_2d(&t[5], current_time.month);
        put_2d(&t[8], current_time.day);
        put_2d(&t[11], current_time.hour);
        put_2d(&t[14], current_time.min);
        put_2d(&t[17], current_time.sec);
        for (int i = 0; i < 19; i++) append_char(&p, end, t[i]);
    } else {
        append_text(&p, end, "----/--/-- --:--:--");
    }

    append_text(&p, end, " | ");
    if (current_dht.ok) {
        append_tenth(&p, end, current_dht.temp10);
        append_text(&p, end, "\xC2\xB0\x43 | ");
        append_tenth(&p, end, current_dht.humd10);
        append_char(&p, end, '%');
    } else {
        append_text(&p, end, "- | -");
    }

    *p = '\0';
}

void format_lcd_temp_line(char* buf, size_t size) {
    if (size == 0) return;

    char* p = buf;
    char* end = buf + size - 1;

    append_text(&p, end, "Temp: ");
    if (current_dht.ok) {
        append_tenth(&p, end, current_dht.temp10);
        append_text(&p, end, "\xDF\x43");
    } else {
        append_text(&p, end, "---.-\xDF\x43");
    }
    *p = '\0';
}

void format_lcd_humd_line(char* buf, size_t size) {
    if (size == 0) return;

    char* p = buf;
    char* end = buf + size - 1;

    append_text(&p, end, "Humd: ");
    if (current_dht.ok) {
        append_tenth(&p, end, current_dht.humd10);
        append_char(&p, end, '%');
    } else {
        append_text(&p, end, "---.-%");
    }
    *p = '\0';
}

void append_status_4(char** p, char* end, bool ok) {
    if (ok) append_text(p, end, "OK  ");
    else append_text(p, end, "ERR ");
}

void format_lcd_status_line(char* buf, size_t size) {
    if (size == 0) return;

    char* p = buf;
    char* end = buf + size - 1;

    append_text(&p, end, "RTC: ");
    append_status_4(&p, end, current_time.ok);
    append_text(&p, end, "  DHT: ");
    append_status_4(&p, end, current_dht.ok);
    *p = '\0';
}

void update_outputs() {
    char buf[64];
    char line[64];

    format_serial_line(line, sizeof(line));
    Serial.println(line);

    format_lcd_time_line(buf, sizeof(buf));
    lcd_write_line(0, buf);

    format_lcd_temp_line(buf, sizeof(buf));
    lcd_write_line(1, buf);

    format_lcd_humd_line(buf, sizeof(buf));
    lcd_write_line(2, buf);

    format_lcd_status_line(buf, sizeof(buf));
    lcd_write_line(3, buf);
}

void update_dht_by_rtc(uint32_t rtc_now) {
    if (!haveDhtRtcSecond) {
        lastDhtRtcSecond = rtc_now;
        haveDhtRtcSecond = true;
        return;
    }

    if (rtc_elapsed_seconds(rtc_now, lastDhtRtcSecond) >= 5) {
        dht_read(&current_dht);
        lastDhtRtcSecond = rtc_now;
        lastDhtMillis = millis();
    }
}

void update_dht_by_millis(unsigned long now) {
    if (now - lastDhtMillis >= 5000) {
        dht_read(&current_dht);
        lastDhtMillis = now;
    }
}

void setup() {
    Serial.begin(115200); delay(1000);
    i2c_init();
    ds1307_init();
    lcd_init();
    dht_read(&current_dht);
    dht_release();
    lastDhtMillis = millis();
}

void loop() {
    unsigned long now = millis();

    if (now - lastRtcPoll < 250) return;
    lastRtcPoll = now;

    ds1307_read(&current_time);
    if (current_time.ok) {
        uint32_t rtc_now = rtc_second_of_day(&current_time);
        if (current_time.sec == lastRtcSecond) {
            return;
        }

        lastRtcSecond = current_time.sec;
        lastFallbackUpdate = now;
        update_outputs();
        update_dht_by_rtc(rtc_now);
    } else {
        lastRtcSecond = 0xFF;
        haveDhtRtcSecond = false;

        if (now - lastFallbackUpdate < 1000) return;
        lastFallbackUpdate = now;
        update_outputs();
        update_dht_by_millis(now);
    }
}
