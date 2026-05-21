/**
 * Hardware I2C Debug Utility for STM32F103 - V2 (Write Test)
 */

#include <Arduino.h>

#define I2C1_BASE      (0x40005400)
#define I2C1_CR1       (*((volatile uint32_t *)(I2C1_BASE + 0x00)))
#define I2C1_CR2       (*((volatile uint32_t *)(I2C1_BASE + 0x04)))
#define I2C1_DR        (*((volatile uint32_t *)(I2C1_BASE + 0x10)))
#define I2C1_SR1       (*((volatile uint32_t *)(I2C1_BASE + 0x14)))
#define I2C1_SR2       (*((volatile uint32_t *)(I2C1_BASE + 0x18)))
#define I2C1_CCR       (*((volatile uint32_t *)(I2C1_BASE + 0x1C)))
#define I2C1_TRISE     (*((volatile uint32_t *)(I2C1_BASE + 0x20)))
#define RCC_APB1ENR    (*((volatile uint32_t *)0x4002101C))
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40021018))
#define GPIOB_CRL      (*((volatile uint32_t *)0x40010C00))

bool hw_i2c_wait_flag(uint32_t flag, bool set) {
    uint32_t timeout = 50000;
    if (set) while (!(I2C1_SR1 & flag)) { if (--timeout == 0) return false; }
    else while (I2C1_SR1 & flag) { if (--timeout == 0) return false; }
    return true;
}

void hw_i2c_init() {
    RCC_APB2ENR |= (1 << 3); 
    RCC_APB1ENR |= (1 << 21);
    GPIOB_CRL &= ~(0xFF000000);
    GPIOB_CRL |= (0xEE000000); // AF-OD 50MHz
    I2C1_CR1 |= (1 << 15); I2C1_CR1 &= ~(1 << 15);
    I2C1_CR2 = 8; I2C1_CCR = 40; I2C1_TRISE = 9;
    I2C1_CR1 |= (1 << 0);
}

bool hw_i2c_start() {
    I2C1_CR1 |= (1 << 8);
    return hw_i2c_wait_flag((1 << 0), true);
}

void hw_i2c_stop() { I2C1_CR1 |= (1 << 9); }

bool hw_i2c_send_addr(uint8_t addr) {
    I2C1_DR = addr;
    if (!hw_i2c_wait_flag((1 << 1), true)) return false;
    (void)I2C1_SR1; (void)I2C1_SR2; // Clear ADDR
    return true;
}

bool hw_i2c_write(uint8_t data) {
    I2C1_DR = data;
    if (!hw_i2c_wait_flag((1 << 7), true)) return false; // Wait for TXE
    return true;
}

uint8_t hw_i2c_read(bool ack) {
    if (ack) I2C1_CR1 |= (1 << 10); else I2C1_CR1 &= ~(1 << 10);
    if (!hw_i2c_wait_flag((1 << 6), true)) return 0; // Wait for RXNE
    return (uint8_t)I2C1_DR;
}

void setup() {
    Serial.begin(115200); delay(2000);
    Serial.println("--- Hardware I2C Debug V2 (Write Test) ---");
    hw_i2c_init();

    // 1. Scanner
    Serial.println("Scanning I2C Bus...");
    for (uint8_t i = 1; i < 127; i++) {
        if (hw_i2c_start()) {
            if (hw_i2c_send_addr(i << 1)) {
                Serial.print("Found device at 0x"); Serial.println(i, HEX);
            }
            hw_i2c_stop();
        }
        delay(10);
    }

    // 2. RTC Write Test
    Serial.println("\nExecuting RTC Write (Setting to 2026/05/21 12:00:00)...");
    if (hw_i2c_start()) {
        if (hw_i2c_send_addr(0x68 << 1)) {
            hw_i2c_write(0x00); // Start at reg 0
            hw_i2c_write(0x00); // Sec 00, Start clock
            hw_i2c_write(0x00); // Min 00
            hw_i2c_write(0x12); // Hour 12
            hw_i2c_write(0x01); // DayOfWeek (not used but skip)
            hw_i2c_write(0x21); // Day 21
            hw_i2c_write(0x05); // Month 05
            hw_i2c_write(0x26); // Year 26
            Serial.println("Write complete.");
        } else Serial.println("Write Addr NACK");
        hw_i2c_stop();
    } else Serial.println("Write Start Failed");

    delay(500);

    // 3. RTC Read Verification
    Serial.println("\nVerifying RTC Read...");
    if (hw_i2c_start()) {
        if (hw_i2c_send_addr(0x68 << 1)) {
            hw_i2c_write(0x00);
            hw_i2c_stop(); delay(10);
            if (hw_i2c_start()) {
                if (hw_i2c_send_addr((0x68 << 1) | 1)) {
                    Serial.print("RTC Raw Data: ");
                    for (int i = 0; i < 7; i++) {
                        uint8_t d = hw_i2c_read(i < 6);
                        Serial.print(d, HEX); Serial.print(" ");
                    }
                    Serial.println();
                } else Serial.println("Read Addr NACK");
            } else Serial.println("Second Start Failed");
        } else Serial.println("First Addr NACK");
        hw_i2c_stop();
    } else Serial.println("First Start Failed");

    Serial.println("\n--- Debug End ---");
}

void loop() {}
