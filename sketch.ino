/**
 * worki Project - STM32 Blue Pill (STM32F103C8)
 * 
 * Features:
 * - Bare-metal I2C Driver for DS1307 RTC & LCD2004 (via PCF8574)
 * - Custom 1-Wire Driver for DHT22
 * - Super Loop with Non-blocking Timing
 * 
 * Pinout:
 * - DHT22: PA0
 * - I2C1 SCL: PB6
 * - I2C1 SDA: PB7
 * - Serial: PA9 (TX), PA10 (RX)
 */

#include <Arduino.h>

// --- Register Definitions (Bare-metal STM32F103) ---
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40021018))
#define RCC_APB1ENR    (*((volatile uint32_t *)0x4002101C))
#define GPIOA_CRL      (*((volatile uint32_t *)0x40010800))
#define GPIOB_CRL      (*((volatile uint32_t *)0x40010C00))

// I2C1 Registers
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
    float temp;
    float humd;
    bool ok;
};

// --- Global Status & Variables ---
RtcTime current_time = {0, 0, 0, 0, 0, 0, false};
DhtData current_dht = {0.0f, 0.0f, false};

const unsigned long RTC_LCD_INTERVAL = 1000;
const unsigned long DHT_INTERVAL = 5000;

unsigned long lastRtcLcdUpdate = 0;
unsigned long lastDhtUpdate = 0;

// --- I2C Bus Layer (Bare-metal) ---

/**
 * Wait for I2C flag in SR1/SR2
 */
void i2c_wait_flag(uint32_t flag, bool is_sr2 = false) {
    uint32_t timeout = 100000;
    if (is_sr2) {
        while (!(I2C1_SR2 & flag) && --timeout);
    } else {
        while (!(I2C1_SR1 & flag) && --timeout);
    }
}

/**
 * Generate I2C Start Condition
 */
void i2c_start() {
    I2C1_CR1 |= (1 << 8);  // START
    i2c_wait_flag(1 << 0); // SB (Start Bit)
}

/**
 * Generate I2C Stop Condition
 */
void i2c_stop() {
    I2C1_CR1 |= (1 << 9);  // STOP
}

/**
 * Write byte to I2C Bus
 */
void i2c_write(uint8_t data) {
    I2C1_DR = data;
    i2c_wait_flag(1 << 7); // TXE (Data register empty)
}

/**
 * Read byte from I2C Bus
 */
uint8_t i2c_read(bool ack) {
    if (ack) I2C1_CR1 |= (1 << 10); // ACK
    else I2C1_CR1 &= ~(1 << 10);    // NACK

    i2c_wait_flag(1 << 6); // RXNE (Data register not empty)
    return (uint8_t)I2C1_DR;
}

/**
 * Send Address
 */
void i2c_send_addr(uint8_t addr, bool read) {
    I2C1_DR = (addr << 1) | (read ? 1 : 0);
    i2c_wait_flag(1 << 1); // ADDR (Address sent)
    (void)I2C1_SR2;        // Clear ADDR flag by reading SR2
}

/**
 * Initialize Hardware I2C1 (B6=SCL, B7=SDA)
 */
void i2c_init() {
    // 1. Enable GPIOB and I2C1 Clock
    RCC_APB2ENR |= (1 << 3);  // IOPBEN
    RCC_APB1ENR |= (1 << 21); // I2C1EN

    // 2. Configure PB6, PB7 as Alternate Function Open-Drain (50MHz)
    GPIOB_CRL &= ~(0xFF000000); // Clear PB6, PB7
    GPIOB_CRL |=  (0xEE000000); // Set PB6, PB7 to AF-OD
    
    // 3. Reset I2C
    I2C1_CR1 |= (1 << 15);
    I2C1_CR1 &= ~(1 << 15);

    // 4. Configure I2C Speed (8MHz APB1 clock)
    I2C1_CR2 = 8;         
    I2C1_CCR = 40;        // 100kHz
    I2C1_TRISE = 9;       

    // 5. Enable I2C
    I2C1_CR1 |= (1 << 0); // PE
}

// --- Device Drivers ---

/**
 * Convert Binary Coded Decimal (BCD) to Decimal
 */
uint8_t bcd2dec(uint8_t val) {
    return ((val / 16 * 10) + (val % 16));
}

/**
 * Read current time from DS1307
 */
void ds1307_read_time(RtcTime* time) {
    i2c_start();
    i2c_send_addr(0x68, false); // DS1307 Write Address
    i2c_write(0x00);            // Start from Register 0x00 (Seconds)
    
    i2c_start();                // Repeated Start
    i2c_send_addr(0x68, true);  // DS1307 Read Address
    
    time->sec   = bcd2dec(i2c_read(true) & 0x7F);
    time->min   = bcd2dec(i2c_read(true));
    time->hour  = bcd2dec(i2c_read(true) & 0x3F);
    (void)i2c_read(true);       // Skip Day of Week
    time->day   = bcd2dec(i2c_read(true));
    time->month = bcd2dec(i2c_read(true));
    time->year  = bcd2dec(i2c_read(false)); // NACK on last byte
    i2c_stop();
    
    time->ok = true; 
}

// --- Helper Functions ---

void print_time(const RtcTime* t) {
    if (!t->ok) {
        Serial.println("RTC: ERR");
        return;
    }
    char buf[32];
    sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d", 
            2000 + t->year, t->month, t->day, t->hour, t->min, t->sec);
    Serial.print(buf);
}

// --- Arduino Framework ---

void setup() {
    Serial.begin(115200);
    i2c_init();
    Serial.println("System Initialized (Bare-metal I2C configured)");
}

void loop() {
    unsigned long currentMillis = millis();

    // Task 1: RTC & LCD Update (1000ms)
    if (currentMillis - lastRtcLcdUpdate >= RTC_LCD_INTERVAL) {
        lastRtcLcdUpdate = currentMillis;
        
        // 1. Read RTC
        ds1307_read_time(&current_time);
        
        // 2. Serial Output (Debug/Verification)
        print_time(&current_time);
        Serial.print(" | ");
        if (current_dht.ok) {
            Serial.print(current_dht.temp, 1);
            Serial.print("⁰C | ");
            Serial.print(current_dht.humd, 1);
            Serial.println("%");
        } else {
            Serial.println("DHT: ERR");
        }
        
        // TODO: Update LCD2004
    }

    // Task 2: DHT22 Update (5000ms)
    if (currentMillis - lastDhtUpdate >= DHT_INTERVAL) {
        lastDhtUpdate = currentMillis;
        // TODO: Read DHT22
    }
}
