#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Default I2C configuration for SSD1306 OLED Display
#define OLED_I2C_PORT       I2C_NUM_0
#define OLED_SDA_PIN        GPIO_NUM_21
#define OLED_SCL_PIN        GPIO_NUM_22
#define OLED_I2C_ADDR       0x3C       // Standard SSD1306 7-bit I2C Address
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

/**
 * @brief Initialize the I2C bus and SSD1306 OLED display subsystem.
 * 
 * @param sda_pin GPIO pin for I2C SDA
 * @param scl_pin GPIO pin for I2C SCL
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t oled_display_init(gpio_num_t sda_pin, gpio_num_t scl_pin);

/**
 * @brief Render current temperature, humidity, pressure, and valve status on OLED.
 * 
 * @param temp Temperature in Celsius
 * @param hum Humidity in Percentage
 * @param pressure Pressure reading
 * @param valve_active Boolean indicating if valve relay is ON/OFF
 */
void oled_display_show_sensor_data(float temp, float hum, float pressure, bool valve_active);

/**
 * @brief Render custom message lines on OLED.
 * 
 * @param line1 First line text (Header/Status)
 * @param line2 Second line text
 * @param line3 Third line text
 */
void oled_display_show_message(const char *line1, const char *line2, const char *line3);

/**
 * @brief Set the firmware version badge displayed on the OLED top header bar.
 * 
 * @param version_str Short version label (e.g. "APP v1.0 [GRN]")
 */
void oled_display_set_fw_version(const char *version_str);

/**
 * @brief Clear OLED buffer and update screen.
 */
void oled_display_clear(void);

/**
 * @brief Run a self-test of the OLED display hardware and test pattern.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t oled_display_test(void);

/**
 * @brief Fill entire screen with a raw byte pattern.
 */
esp_err_t oled_display_fill_raw(uint8_t pattern);

/**
 * @brief Get the active detected 7-bit I2C address of the OLED.
 */
uint8_t oled_display_get_addr(void);

/**
 * @brief Force all pixels ON (0xA5) regardless of RAM content, or resume RAM content (0xA4).
 */
void oled_display_force_all_on(bool force_on);

#ifdef __cplusplus
}
#endif

#endif // OLED_DISPLAY_H
