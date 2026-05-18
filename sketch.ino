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

// --- Timing Constants ---
const unsigned long RTC_LCD_INTERVAL = 1000;
const unsigned long DHT_INTERVAL = 5000;

unsigned long lastRtcLcdUpdate = 0;
unsigned long lastDhtUpdate = 0;

// --- Global Status ---
bool rtc_ok = false;
bool dht_ok = false;

// --- Drivers Placeholder ---

/**
 * Initialize Hardware I2C1 (B6=SCL, B7=SDA)
 */
void i2c_init() {
    // 1. Enable GPIOB and I2C1 Clock
    RCC_APB2ENR |= (1 << 3);  // IOPBEN
    RCC_APB1ENR |= (1 << 21); // I2C1EN

    // 2. Configure PB6, PB7 as Alternate Function Open-Drain (50MHz)
    // CNF=11 (AF-OD), MODE=11 (Output 50MHz) -> 0xF
    GPIOB_CRL &= ~(0xFF000000); // Clear PB6, PB7
    GPIOB_CRL |=  (0xEE000000); // Set PB6, PB7 to AF-OD 2MHz (E=1110, but 0xF=1111 is safer for high speed)
    
    // 3. Reset I2C
    I2C1_CR1 |= (1 << 15);
    I2C1_CR1 &= ~(1 << 15);

    // 4. Configure I2C Speed (Assuming 8MHz APB1 clock for simplicity in Wokwi)
    I2C1_CR2 = 8;         // 8MHz
    I2C1_CCR = 40;        // 100kHz Standard Mode (8MHz / (2 * 100kHz))
    I2C1_TRISE = 9;       // 8MHz + 1

    // 5. Enable I2C
    I2C1_CR1 |= (1 << 0); // PE
}

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
        // TODO: Read DS1307
        // TODO: Update LCD2004
        // TODO: Update Serial
        Serial.println("Updating RTC & LCD...");
    }

    // Task 2: DHT22 Update (5000ms)
    if (currentMillis - lastDhtUpdate >= DHT_INTERVAL) {
        lastDhtUpdate = currentMillis;
        // TODO: Read DHT22 (1-Wire)
        Serial.println("Reading DHT22...");
    }
}
