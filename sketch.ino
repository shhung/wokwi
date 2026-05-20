/*
 * STM32 Blue Pill Wokwi sketch
 *
 * Default mode is the final application. Change SELF_TEST_MODE to run
 * independent bottom-up verification:
 *   0: Application
 *   1: Tier 1 bus test
 *   2: Tier 2 device test
 *   3: Tier 3 scheduler test
 */

#ifndef SELF_TEST_MODE
#define SELF_TEST_MODE 0
#endif

#if defined(PA0)
static const uint8_t PIN_DHT = PA0;
#elif defined(A0)
static const uint8_t PIN_DHT = A0;
#else
#error "DHT pin for STM32 Blue Pill must be PA0/A0."
#endif

#if defined(PB7)
static const uint8_t PIN_I2C_SDA = PB7;
#elif defined(B7)
static const uint8_t PIN_I2C_SDA = B7;
#else
#error "I2C SDA pin for STM32 Blue Pill must be PB7/B7."
#endif

#if defined(PB6)
static const uint8_t PIN_I2C_SCL = PB6;
#elif defined(B6)
static const uint8_t PIN_I2C_SCL = B6;
#else
#error "I2C SCL pin for STM32 Blue Pill must be PB6/B6."
#endif

#define MODE_APP             0
#define MODE_TIER1_BUS_TEST  1
#define MODE_TIER2_DEV_TEST  2
#define MODE_TIER3_SCH_TEST  3

#define DS1307_ADDR          0x68
#define LCD2004_ADDR         0x27

#define I2C_DELAY_US         5U
#define I2C_TIMEOUT_US       1000U

#define DHT_START_LOW_US     1100U
#define DHT_RELEASE_US       35U
#define DHT_EDGE_TIMEOUT_US  120U
#define DHT_BIT_ONE_US       45U

#define RTC_INTERVAL_MS      800UL
#define DHT_INTERVAL_MS      5000UL
#define OUTPUT_INTERVAL_MS   1000UL

#define LCD_COLS             20U
#define LCD_ROWS             4U
#define LCD_RS               0x01U
#define LCD_EN               0x04U
#define LCD_BACKLIGHT        0x08U
#define LCD_DEGREE_CHAR      0xDFU
#define SERIAL_DEGREE_C      "\xC2\xB0" "C"

typedef struct {
  bool ok;
  uint8_t sec;
  uint8_t min;
  uint8_t hour;
  uint8_t day;
  uint8_t month;
  uint16_t year;
} rtc_state_t;

typedef struct {
  bool ok;
  int16_t temp_x10;
  int16_t hum_x10;
} dht_state_t;

static rtc_state_t g_rtc;
static dht_state_t g_dht;

static bool g_lcd_ready;
static uint32_t g_last_rtc_ms;
static uint32_t g_last_dht_ms;
static uint32_t g_last_output_ms;
static uint32_t g_last_test_ms;

static uint8_t g_i2c_read_buf[8];
static uint8_t g_i2c_write_buf[2];
static uint8_t g_dht_raw[5];

static char g_lcd_line1[LCD_COLS + 1U];
static char g_lcd_line2[LCD_COLS + 1U];
static char g_lcd_line3[LCD_COLS + 1U];
static char g_lcd_line4[LCD_COLS + 1U];
static char g_temp_text[12];
static char g_hum_text[12];
static char g_serial_line[96];

static void pin_drive_low(uint8_t pin)
{
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
}

static void pin_release_high(uint8_t pin)
{
  pinMode(pin, INPUT_PULLUP);
}

static void i2c_release_sda(void)
{
  pin_release_high(PIN_I2C_SDA);
}

static void i2c_release_scl(void)
{
  pin_release_high(PIN_I2C_SCL);
}

static void i2c_drive_sda_low(void)
{
  pin_drive_low(PIN_I2C_SDA);
}

static void i2c_drive_scl_low(void)
{
  pin_drive_low(PIN_I2C_SCL);
}

static bool i2c_wait_scl_high(void)
{
  uint32_t start_us = micros();

  while (digitalRead(PIN_I2C_SCL) == LOW) {
    if ((uint32_t)(micros() - start_us) >= I2C_TIMEOUT_US) {
      return false;
    }
  }

  return true;
}

static bool i2c_recover_bus(void)
{
  uint8_t i;

  i2c_release_sda();
  i2c_release_scl();
  delayMicroseconds(I2C_DELAY_US);

  if (!i2c_wait_scl_high()) {
    return false;
  }

  for (i = 0U; i < 9U && digitalRead(PIN_I2C_SDA) == LOW; i++) {
    i2c_drive_scl_low();
    delayMicroseconds(I2C_DELAY_US);
    i2c_release_scl();
    if (!i2c_wait_scl_high()) {
      return false;
    }
    delayMicroseconds(I2C_DELAY_US);
  }

  i2c_drive_sda_low();
  delayMicroseconds(I2C_DELAY_US);
  i2c_release_scl();
  if (!i2c_wait_scl_high()) {
    return false;
  }
  delayMicroseconds(I2C_DELAY_US);
  i2c_release_sda();
  delayMicroseconds(I2C_DELAY_US);

  return (digitalRead(PIN_I2C_SCL) == HIGH) &&
         (digitalRead(PIN_I2C_SDA) == HIGH);
}

static void i2c_init(void)
{
  i2c_release_sda();
  i2c_release_scl();
  delayMicroseconds(I2C_DELAY_US);
  (void)i2c_recover_bus();
}

static bool i2c_start_condition(void)
{
  i2c_release_sda();
  i2c_release_scl();
  if (!i2c_wait_scl_high()) {
    return false;
  }

  if (digitalRead(PIN_I2C_SDA) == LOW) {
    if (!i2c_recover_bus()) {
      return false;
    }
  }

  delayMicroseconds(I2C_DELAY_US);
  i2c_drive_sda_low();
  delayMicroseconds(I2C_DELAY_US);
  i2c_drive_scl_low();
  delayMicroseconds(I2C_DELAY_US);

  return true;
}

static bool i2c_restart_condition(void)
{
  i2c_release_sda();
  delayMicroseconds(I2C_DELAY_US);
  i2c_release_scl();
  if (!i2c_wait_scl_high()) {
    return false;
  }
  delayMicroseconds(I2C_DELAY_US);
  i2c_drive_sda_low();
  delayMicroseconds(I2C_DELAY_US);
  i2c_drive_scl_low();
  delayMicroseconds(I2C_DELAY_US);

  return true;
}

static void i2c_stop_condition(void)
{
  i2c_drive_sda_low();
  delayMicroseconds(I2C_DELAY_US);
  i2c_release_scl();
  (void)i2c_wait_scl_high();
  delayMicroseconds(I2C_DELAY_US);
  i2c_release_sda();
  delayMicroseconds(I2C_DELAY_US);
}

static bool i2c_write_byte(uint8_t data)
{
  uint8_t mask;
  bool ack;

  for (mask = 0x80U; mask != 0U; mask >>= 1U) {
    if ((data & mask) != 0U) {
      i2c_release_sda();
    } else {
      i2c_drive_sda_low();
    }

    delayMicroseconds(I2C_DELAY_US);
    i2c_release_scl();
    if (!i2c_wait_scl_high()) {
      return false;
    }
    delayMicroseconds(I2C_DELAY_US);
    i2c_drive_scl_low();
    delayMicroseconds(I2C_DELAY_US);
  }

  i2c_release_sda();
  delayMicroseconds(I2C_DELAY_US);
  i2c_release_scl();
  if (!i2c_wait_scl_high()) {
    return false;
  }
  delayMicroseconds(I2C_DELAY_US);
  ack = (digitalRead(PIN_I2C_SDA) == LOW);
  i2c_drive_scl_low();
  delayMicroseconds(I2C_DELAY_US);

  return ack;
}

static uint8_t i2c_read_byte(bool ack, bool *ok)
{
  uint8_t value = 0U;
  uint8_t bit;

  i2c_release_sda();

  for (bit = 0U; bit < 8U; bit++) {
    value <<= 1U;
    i2c_release_scl();
    if (!i2c_wait_scl_high()) {
      *ok = false;
      i2c_drive_scl_low();
      return value;
    }
    delayMicroseconds(I2C_DELAY_US);
    if (digitalRead(PIN_I2C_SDA) == HIGH) {
      value |= 1U;
    }
    i2c_drive_scl_low();
    delayMicroseconds(I2C_DELAY_US);
  }

  if (ack) {
    i2c_drive_sda_low();
  } else {
    i2c_release_sda();
  }

  delayMicroseconds(I2C_DELAY_US);
  i2c_release_scl();
  if (!i2c_wait_scl_high()) {
    *ok = false;
    i2c_drive_scl_low();
    i2c_release_sda();
    return value;
  }
  delayMicroseconds(I2C_DELAY_US);
  i2c_drive_scl_low();
  i2c_release_sda();
  delayMicroseconds(I2C_DELAY_US);

  return value;
}

static bool i2c_write_data(uint8_t addr, const uint8_t *data, uint8_t len)
{
  uint8_t i;
  bool ok = true;

  if (!i2c_start_condition()) {
    return false;
  }

  if (!i2c_write_byte((uint8_t)(addr << 1U))) {
    ok = false;
  }

  for (i = 0U; ok && i < len; i++) {
    if (!i2c_write_byte(data[i])) {
      ok = false;
    }
  }

  i2c_stop_condition();
  return ok;
}

static bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
  uint8_t i;
  bool ok = true;

  if (len == 0U) {
    return true;
  }

  if (!i2c_start_condition()) {
    return false;
  }

  if (!i2c_write_byte((uint8_t)(addr << 1U))) {
    ok = false;
  }

  if (ok && !i2c_write_byte(reg)) {
    ok = false;
  }

  if (ok && !i2c_restart_condition()) {
    ok = false;
  }

  if (ok && !i2c_write_byte((uint8_t)((addr << 1U) | 0x01U))) {
    ok = false;
  }

  for (i = 0U; ok && i < len; i++) {
    buf[i] = i2c_read_byte(i < (uint8_t)(len - 1U), &ok);
  }

  i2c_stop_condition();
  return ok;
}

static bool i2c_ping(uint8_t addr)
{
  bool ok;

  if (!i2c_start_condition()) {
    return false;
  }
  ok = i2c_write_byte((uint8_t)(addr << 1U));
  i2c_stop_condition();

  return ok;
}

static bool bcd_decode(uint8_t bcd, uint8_t *out)
{
  uint8_t high = (uint8_t)(bcd >> 4U);
  uint8_t low = (uint8_t)(bcd & 0x0FU);

  if (high > 9U || low > 9U) {
    return false;
  }

  *out = (uint8_t)(high * 10U + low);
  return true;
}

static bool rtc_is_leap_year(uint16_t year)
{
  return ((year % 4U) == 0U) &&
         (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month)
{
  static const uint8_t days_by_month[12] = {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };

  if (month == 2U && rtc_is_leap_year(year)) {
    return 29U;
  }

  if (month < 1U || month > 12U) {
    return 0U;
  }

  return days_by_month[month - 1U];
}

static bool rtc_read_time(void)
{
  uint8_t sec;
  uint8_t min;
  uint8_t hour;
  uint8_t day;
  uint8_t date;
  uint8_t month;
  uint8_t year;
  uint8_t max_day;
  uint8_t hour_reg;

  if (!i2c_read_reg(DS1307_ADDR, 0x00U, g_i2c_read_buf, 7U)) {
    return false;
  }

  if ((g_i2c_read_buf[0] & 0x80U) != 0U) {
    return false;
  }

  if (!bcd_decode((uint8_t)(g_i2c_read_buf[0] & 0x7FU), &sec) || sec > 59U) {
    return false;
  }

  if (!bcd_decode((uint8_t)(g_i2c_read_buf[1] & 0x7FU), &min) || min > 59U) {
    return false;
  }

  hour_reg = g_i2c_read_buf[2];
  if ((hour_reg & 0x40U) != 0U) {
    if (!bcd_decode((uint8_t)(hour_reg & 0x1FU), &hour)) {
      return false;
    }
    if (hour < 1U || hour > 12U) {
      return false;
    }
    if ((hour_reg & 0x20U) != 0U) {
      if (hour != 12U) {
        hour = (uint8_t)(hour + 12U);
      }
    } else if (hour == 12U) {
      hour = 0U;
    }
  } else {
    if (!bcd_decode((uint8_t)(hour_reg & 0x3FU), &hour) || hour > 23U) {
      return false;
    }
  }

  if (!bcd_decode((uint8_t)(g_i2c_read_buf[3] & 0x07U), &day)) {
    return false;
  }
  if (day < 1U || day > 7U) {
    return false;
  }

  if (!bcd_decode((uint8_t)(g_i2c_read_buf[4] & 0x3FU), &date)) {
    return false;
  }
  if (!bcd_decode((uint8_t)(g_i2c_read_buf[5] & 0x1FU), &month)) {
    return false;
  }
  if (!bcd_decode(g_i2c_read_buf[6], &year)) {
    return false;
  }

  g_rtc.year = (uint16_t)(2000U + year);
  max_day = rtc_days_in_month(g_rtc.year, month);
  if (date < 1U || max_day == 0U || date > max_day) {
    return false;
  }

  g_rtc.sec = sec;
  g_rtc.min = min;
  g_rtc.hour = hour;
  g_rtc.day = date;
  g_rtc.month = month;

  return true;
}

static bool lcd_pcf_write(uint8_t data)
{
  g_i2c_write_buf[0] = data;
  return i2c_write_data(LCD2004_ADDR, g_i2c_write_buf, 1U);
}

static bool lcd_write4(uint8_t nibble, uint8_t flags)
{
  uint8_t out = (uint8_t)((nibble & 0xF0U) | flags | LCD_BACKLIGHT);

  if (!lcd_pcf_write((uint8_t)(out | LCD_EN))) {
    return false;
  }
  delayMicroseconds(1U);

  if (!lcd_pcf_write((uint8_t)(out & (uint8_t)~LCD_EN))) {
    return false;
  }
  delayMicroseconds(50U);

  return true;
}

static bool lcd_send(uint8_t value, uint8_t flags)
{
  if (!lcd_write4((uint8_t)(value & 0xF0U), flags)) {
    return false;
  }

  if (!lcd_write4((uint8_t)((value << 4U) & 0xF0U), flags)) {
    return false;
  }

  return true;
}

static bool lcd_command(uint8_t cmd)
{
  bool ok = lcd_send(cmd, 0U);

  if (cmd == 0x01U || cmd == 0x02U) {
    delayMicroseconds(2000U);
  }

  if (!ok) {
    g_lcd_ready = false;
  }

  return ok;
}

static bool lcd_data(uint8_t data)
{
  bool ok = lcd_send(data, LCD_RS);

  if (!ok) {
    g_lcd_ready = false;
  }

  return ok;
}

static bool lcd_init(void)
{
  if (!i2c_ping(LCD2004_ADDR)) {
    g_lcd_ready = false;
    return false;
  }

  delay(50);

  if (!lcd_write4(0x30U, 0U)) {
    g_lcd_ready = false;
    return false;
  }
  delayMicroseconds(4500U);

  if (!lcd_write4(0x30U, 0U)) {
    g_lcd_ready = false;
    return false;
  }
  delayMicroseconds(4500U);

  if (!lcd_write4(0x30U, 0U)) {
    g_lcd_ready = false;
    return false;
  }
  delayMicroseconds(150U);

  if (!lcd_write4(0x20U, 0U)) {
    g_lcd_ready = false;
    return false;
  }

  if (!lcd_command(0x28U)) {
    return false;
  }
  if (!lcd_command(0x08U)) {
    return false;
  }
  if (!lcd_command(0x01U)) {
    return false;
  }
  if (!lcd_command(0x06U)) {
    return false;
  }
  if (!lcd_command(0x0CU)) {
    return false;
  }

  g_lcd_ready = true;
  return true;
}

static bool lcd_set_cursor(uint8_t row, uint8_t col)
{
  static const uint8_t row_offset[LCD_ROWS] = { 0x00U, 0x40U, 0x14U, 0x54U };

  if (row >= LCD_ROWS || col >= LCD_COLS) {
    return false;
  }

  return lcd_command((uint8_t)(0x80U | (row_offset[row] + col)));
}

static bool lcd_write_text_row(uint8_t row, const char *text)
{
  uint8_t i;

  if (!g_lcd_ready && !lcd_init()) {
    return false;
  }

  if (!lcd_set_cursor(row, 0U)) {
    return false;
  }

  for (i = 0U; i < LCD_COLS; i++) {
    if (!lcd_data((uint8_t)text[i])) {
      return false;
    }
  }

  return true;
}

static bool dht_wait_level(uint8_t level, uint32_t timeout_us)
{
  uint32_t start_us = micros();

  while (digitalRead(PIN_DHT) != level) {
    if ((uint32_t)(micros() - start_us) >= timeout_us) {
      return false;
    }
  }

  return true;
}

static bool dht_read_sample(void)
{
  uint8_t i;
  uint8_t byte_idx;
  uint32_t pulse_start_us;
  uint32_t high_width_us;
  uint16_t raw_hum;
  uint16_t raw_temp;
  uint8_t checksum;

  for (i = 0U; i < 5U; i++) {
    g_dht_raw[i] = 0U;
  }

  pin_drive_low(PIN_DHT);
  delayMicroseconds(DHT_START_LOW_US);
  pin_release_high(PIN_DHT);
  delayMicroseconds(DHT_RELEASE_US);

  if (!dht_wait_level(LOW, DHT_EDGE_TIMEOUT_US)) {
    return false;
  }
  if (!dht_wait_level(HIGH, DHT_EDGE_TIMEOUT_US)) {
    return false;
  }
  if (!dht_wait_level(LOW, DHT_EDGE_TIMEOUT_US)) {
    return false;
  }

  for (i = 0U; i < 40U; i++) {
    if (!dht_wait_level(HIGH, DHT_EDGE_TIMEOUT_US)) {
      return false;
    }

    pulse_start_us = micros();
    if (!dht_wait_level(LOW, DHT_EDGE_TIMEOUT_US)) {
      return false;
    }
    high_width_us = (uint32_t)(micros() - pulse_start_us);

    byte_idx = (uint8_t)(i >> 3U);
    g_dht_raw[byte_idx] <<= 1U;
    if (high_width_us > DHT_BIT_ONE_US) {
      g_dht_raw[byte_idx] |= 1U;
    }
  }

  checksum = (uint8_t)(g_dht_raw[0] + g_dht_raw[1] +
                       g_dht_raw[2] + g_dht_raw[3]);
  if (checksum != g_dht_raw[4]) {
    return false;
  }

  raw_hum = (uint16_t)(((uint16_t)g_dht_raw[0] << 8U) | g_dht_raw[1]);
  raw_temp = (uint16_t)(((uint16_t)(g_dht_raw[2] & 0x7FU) << 8U) |
                         g_dht_raw[3]);

  if (raw_hum > 1000U || raw_temp > 1250U) {
    return false;
  }

  g_dht.hum_x10 = (int16_t)raw_hum;
  if ((g_dht_raw[2] & 0x80U) != 0U) {
    g_dht.temp_x10 = (int16_t)(0 - (int16_t)raw_temp);
  } else {
    g_dht.temp_x10 = (int16_t)raw_temp;
  }

  return true;
}

static void pad_lcd_line(char *line)
{
  uint8_t i = 0U;

  while (i < LCD_COLS && line[i] != '\0') {
    i++;
  }

  while (i < LCD_COLS) {
    line[i++] = ' ';
  }

  line[LCD_COLS] = '\0';
}

static void format_x10(char *out, size_t out_len, int16_t value_x10, bool valid)
{
  int32_t value;

  if (!valid) {
    (void)snprintf(out, out_len, "---.-");
    return;
  }

  value = value_x10;
  if (value < 0) {
    value = -value;
    (void)snprintf(out, out_len, "-%ld.%ld",
                   (long)(value / 10L), (long)(value % 10L));
  } else {
    (void)snprintf(out, out_len, "%ld.%ld",
                   (long)(value / 10L), (long)(value % 10L));
  }
}

static void build_output_lines(void)
{
  format_x10(g_temp_text, sizeof(g_temp_text), g_dht.temp_x10, g_dht.ok);
  format_x10(g_hum_text, sizeof(g_hum_text), g_dht.hum_x10, g_dht.ok);

  if (g_rtc.ok) {
    (void)snprintf(g_lcd_line1, sizeof(g_lcd_line1),
                   "%02u:%02u:%02u  %04u/%02u/%02u",
                   (unsigned int)g_rtc.hour,
                   (unsigned int)g_rtc.min,
                   (unsigned int)g_rtc.sec,
                   (unsigned int)g_rtc.year,
                   (unsigned int)g_rtc.month,
                   (unsigned int)g_rtc.day);
  } else {
    (void)snprintf(g_lcd_line1, sizeof(g_lcd_line1),
                   "--:--:--  ----/--/--");
  }

  (void)snprintf(g_lcd_line2, sizeof(g_lcd_line2), "Temp: %s%cC",
                 g_temp_text, (int)LCD_DEGREE_CHAR);
  (void)snprintf(g_lcd_line3, sizeof(g_lcd_line3), "Humd: %s%%", g_hum_text);
  (void)snprintf(g_lcd_line4, sizeof(g_lcd_line4), "RTC: %s   DHT: %s",
                 g_rtc.ok ? "OK" : "ERR", g_dht.ok ? "OK" : "ERR");

  pad_lcd_line(g_lcd_line1);
  pad_lcd_line(g_lcd_line2);
  pad_lcd_line(g_lcd_line3);
  pad_lcd_line(g_lcd_line4);

  if (g_rtc.ok) {
    (void)snprintf(g_serial_line, sizeof(g_serial_line),
                   "%04u/%02u/%02u %02u:%02u:%02u | %s" SERIAL_DEGREE_C
                   " | %s%%",
                   (unsigned int)g_rtc.year,
                   (unsigned int)g_rtc.month,
                   (unsigned int)g_rtc.day,
                   (unsigned int)g_rtc.hour,
                   (unsigned int)g_rtc.min,
                   (unsigned int)g_rtc.sec,
                   g_temp_text, g_hum_text);
  } else {
    (void)snprintf(g_serial_line, sizeof(g_serial_line),
                   "----/--/-- --:--:-- | %s" SERIAL_DEGREE_C " | %s%%",
                   g_temp_text, g_hum_text);
  }
}

static void update_lcd_output(void)
{
  if (!g_lcd_ready && !lcd_init()) {
    return;
  }

  if (!lcd_write_text_row(0U, g_lcd_line1)) {
    return;
  }
  if (!lcd_write_text_row(1U, g_lcd_line2)) {
    return;
  }
  if (!lcd_write_text_row(2U, g_lcd_line3)) {
    return;
  }
  (void)lcd_write_text_row(3U, g_lcd_line4);
}

static void emit_application_output(void)
{
  build_output_lines();
  update_lcd_output();
  Serial.println(g_serial_line);
  Serial.flush();
}

static void app_setup(void)
{
  uint32_t now;

  Serial.begin(115200);
  i2c_init();
  pin_release_high(PIN_DHT);

  g_rtc.ok = false;
  g_dht.ok = false;
  g_dht.temp_x10 = 0;
  g_dht.hum_x10 = 0;
  g_lcd_ready = lcd_init();

  now = millis();
  g_last_rtc_ms = (uint32_t)(now - RTC_INTERVAL_MS);
  g_last_dht_ms = (uint32_t)(now - DHT_INTERVAL_MS);
  g_last_output_ms = (uint32_t)(now - OUTPUT_INTERVAL_MS);
}

static void app_loop(void)
{
  uint32_t now = millis();

  if ((uint32_t)(now - g_last_rtc_ms) >= RTC_INTERVAL_MS) {
    g_last_rtc_ms = now;
    g_rtc.ok = rtc_read_time();
  }

  if ((uint32_t)(now - g_last_dht_ms) >= DHT_INTERVAL_MS) {
    g_last_dht_ms = now;
    g_dht.ok = dht_read_sample();
  }

  if ((uint32_t)(now - g_last_output_ms) >= OUTPUT_INTERVAL_MS) {
    g_last_output_ms = now;
    emit_application_output();
  }
}

static void bus_test_setup(void)
{
  uint8_t addr;

  Serial.begin(115200);
  i2c_init();
  pin_release_high(PIN_DHT);

  Serial.println("Tier1 Bus Test");
  for (addr = 1U; addr < 0x7FU; addr++) {
    if (i2c_ping(addr)) {
      (void)snprintf(g_serial_line, sizeof(g_serial_line),
                     "I2C device found: 0x%02X", (unsigned int)addr);
      Serial.println(g_serial_line);
    }
  }
  Serial.flush();

  g_last_test_ms = (uint32_t)(millis() - OUTPUT_INTERVAL_MS);
}

static void bus_test_loop(void)
{
  uint32_t now = millis();

  if ((uint32_t)(now - g_last_test_ms) < OUTPUT_INTERVAL_MS) {
    return;
  }

  g_last_test_ms = now;
  (void)snprintf(g_serial_line, sizeof(g_serial_line),
                 "%lu | RTC_I2C:%s LCD_I2C:%s DHT_IDLE:%s",
                 (unsigned long)now,
                 i2c_ping(DS1307_ADDR) ? "OK" : "ERR",
                 i2c_ping(LCD2004_ADDR) ? "OK" : "ERR",
                 digitalRead(PIN_DHT) == HIGH ? "HIGH" : "LOW");
  Serial.println(g_serial_line);
  Serial.flush();
}

static void device_test_setup(void)
{
  Serial.begin(115200);
  i2c_init();
  pin_release_high(PIN_DHT);
  g_lcd_ready = lcd_init();

  Serial.println("Tier2 Device Test");
  Serial.flush();
  g_last_test_ms = (uint32_t)(millis() - DHT_INTERVAL_MS);
}

static void device_test_loop(void)
{
  uint32_t now = millis();

  if ((uint32_t)(now - g_last_test_ms) < DHT_INTERVAL_MS) {
    return;
  }

  g_last_test_ms = now;
  g_rtc.ok = rtc_read_time();
  g_dht.ok = dht_read_sample();
  emit_application_output();
}

static void scheduler_test_setup(void)
{
  uint32_t now;

  Serial.begin(115200);
  i2c_init();
  pin_release_high(PIN_DHT);
  g_lcd_ready = lcd_init();

  now = millis();
  g_last_rtc_ms = (uint32_t)(now - RTC_INTERVAL_MS);
  g_last_dht_ms = (uint32_t)(now - DHT_INTERVAL_MS);
  g_last_output_ms = (uint32_t)(now - OUTPUT_INTERVAL_MS);

  Serial.println("Tier3 Scheduler Test");
  Serial.flush();
}

static void scheduler_test_loop(void)
{
  uint32_t now = millis();

  if ((uint32_t)(now - g_last_rtc_ms) >= RTC_INTERVAL_MS) {
    g_last_rtc_ms = now;
    g_rtc.ok = rtc_read_time();
    (void)snprintf(g_serial_line, sizeof(g_serial_line),
                   "%lu | rtc_task | %s", (unsigned long)now,
                   g_rtc.ok ? "OK" : "ERR");
    Serial.println(g_serial_line);
    Serial.flush();
  }

  now = millis();
  if ((uint32_t)(now - g_last_dht_ms) >= DHT_INTERVAL_MS) {
    g_last_dht_ms = now;
    g_dht.ok = dht_read_sample();
    (void)snprintf(g_serial_line, sizeof(g_serial_line),
                   "%lu | dht_task | %s", (unsigned long)now,
                   g_dht.ok ? "OK" : "ERR");
    Serial.println(g_serial_line);
    Serial.flush();
  }

  now = millis();
  if ((uint32_t)(now - g_last_output_ms) >= OUTPUT_INTERVAL_MS) {
    g_last_output_ms = now;
    build_output_lines();
    (void)snprintf(g_serial_line, sizeof(g_serial_line),
                   "%lu | output_task | RTC:%s DHT:%s",
                   (unsigned long)now,
                   g_rtc.ok ? "OK" : "ERR",
                   g_dht.ok ? "OK" : "ERR");
    Serial.println(g_serial_line);
    Serial.flush();
  }
}

void setup()
{
#if SELF_TEST_MODE == MODE_TIER1_BUS_TEST
  bus_test_setup();
#elif SELF_TEST_MODE == MODE_TIER2_DEV_TEST
  device_test_setup();
#elif SELF_TEST_MODE == MODE_TIER3_SCH_TEST
  scheduler_test_setup();
#else
  app_setup();
#endif
}

void loop()
{
#if SELF_TEST_MODE == MODE_TIER1_BUS_TEST
  bus_test_loop();
#elif SELF_TEST_MODE == MODE_TIER2_DEV_TEST
  device_test_loop();
#elif SELF_TEST_MODE == MODE_TIER3_SCH_TEST
  scheduler_test_loop();
#else
  app_loop();
#endif
}
