#include "oled_display.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OLED_DISPLAY";

static uint8_t s_display_buffer[OLED_WIDTH * OLED_HEIGHT / 8];
static SemaphoreHandle_t s_i2c_mutex = NULL;
static bool s_initialized = false;
static uint8_t s_oled_addr = OLED_I2C_ADDR;

// Basic 8x8 ASCII Font table (ASCII 32 to 127)
static const uint8_t font8x8_basic[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00}, // 33 !
    {0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00}, // 34 "
    {0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00, 0x00, 0x00}, // 35 #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00, 0x00, 0x00}, // 36 $
    {0x23, 0x13, 0x08, 0x64, 0x62, 0x00, 0x00, 0x00}, // 37 %
    {0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x00, 0x00}, // 38 &
    {0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // 39 '
    {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00}, // 40 (
    {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00}, // 41 )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00, 0x00, 0x00}, // 42 *
    {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00}, // 43 +
    {0x00, 0x50, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00}, // 44 ,
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00}, // 45 -
    {0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00}, // 46 .
    {0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00}, // 47 /
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x00, 0x00}, // 48 0
    {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x00, 0x00}, // 49 1
    {0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00}, // 50 2
    {0x21, 0x41, 0x45, 0x4B, 0x31, 0x00, 0x00, 0x00}, // 51 3
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, 0x00, 0x00}, // 52 4
    {0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00}, // 53 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00, 0x00, 0x00}, // 54 6
    {0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00, 0x00}, // 55 7
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00}, // 56 8
    {0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00, 0x00}, // 57 9
    {0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00}, // 58 :
    {0x00, 0x56, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00}, // 59 ;
    {0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00}, // 60 <
    {0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00}, // 61 =
    {0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00, 0x00}, // 62 >
    {0x02, 0x01, 0x51, 0x09, 0x06, 0x00, 0x00, 0x00}, // 63 ?
    {0x32, 0x49, 0x79, 0x41, 0x3E, 0x00, 0x00, 0x00}, // 64 @
    {0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00, 0x00, 0x00}, // 65 A
    {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00}, // 66 B
    {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00}, // 67 C
    {0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00}, // 68 D
    {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00}, // 69 E
    {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00, 0x00, 0x00}, // 70 F
    {0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00, 0x00, 0x00}, // 71 G
    {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x00, 0x00}, // 72 H
    {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00, 0x00}, // 73 I
    {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, 0x00, 0x00}, // 74 J
    {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00}, // 75 K
    {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00}, // 76 L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00, 0x00, 0x00}, // 77 M
    {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, 0x00, 0x00}, // 78 N
    {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, 0x00, 0x00}, // 79 O
    {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00}, // 80 P
    {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00, 0x00, 0x00}, // 81 Q
    {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00, 0x00}, // 82 R
    {0x26, 0x49, 0x49, 0x49, 0x32, 0x00, 0x00, 0x00}, // 83 S
    {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, 0x00, 0x00}, // 84 T
    {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, 0x00, 0x00}, // 85 U
    {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, 0x00, 0x00}, // 86 V
    {0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00, 0x00, 0x00}, // 87 W
    {0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00, 0x00}, // 88 X
    {0x07, 0x08, 0x70, 0x08, 0x07, 0x00, 0x00, 0x00}, // 89 Y
    {0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00, 0x00}, // 90 Z
    {0x00, 0x7F, 0x41, 0x41, 0x00, 0x00, 0x00, 0x00}, // 91 [
    {0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00}, // 92 backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00, 0x00, 0x00, 0x00}, // 93 ]
    {0x04, 0x02, 0x01, 0x02, 0x04, 0x00, 0x00, 0x00}, // 94 ^
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00}, // 95 _
    {0x00, 0x01, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00}, // 96 `
    {0x20, 0x54, 0x54, 0x54, 0x78, 0x00, 0x00, 0x00}, // 97 a
    {0x7F, 0x48, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00}, // 98 b
    {0x38, 0x44, 0x44, 0x44, 0x20, 0x00, 0x00, 0x00}, // 99 c
    {0x38, 0x44, 0x44, 0x48, 0x7F, 0x00, 0x00, 0x00}, // 100 d
    {0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x00, 0x00}, // 101 e
    {0x08, 0x7E, 0x09, 0x01, 0x02, 0x00, 0x00, 0x00}, // 102 f
    {0x18, 0xA4, 0xA4, 0xA4, 0x7C, 0x00, 0x00, 0x00}, // 103 g
    {0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00, 0x00}, // 104 h
    {0x00, 0x44, 0x7D, 0x40, 0x00, 0x00, 0x00, 0x00}, // 105 i
    {0x20, 0x40, 0x44, 0x3D, 0x00, 0x00, 0x00, 0x00}, // 106 j
    {0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00}, // 107 k
    {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00, 0x00, 0x00}, // 108 l
    {0x7C, 0x04, 0x18, 0x04, 0x78, 0x00, 0x00, 0x00}, // 109 m
    {0x7C, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00, 0x00}, // 110 n
    {0x38, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00}, // 111 o
    {0xFC, 0x24, 0x24, 0x24, 0x18, 0x00, 0x00, 0x00}, // 112 p
    {0x18, 0x24, 0x24, 0x18, 0xFC, 0x00, 0x00, 0x00}, // 113 q
    {0x7C, 0x08, 0x04, 0x04, 0x08, 0x00, 0x00, 0x00}, // 114 r
    {0x48, 0x54, 0x54, 0x54, 0x24, 0x00, 0x00, 0x00}, // 115 s
    {0x04, 0x3E, 0x44, 0x24, 0x00, 0x00, 0x00, 0x00}, // 116 t
    {0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00, 0x00, 0x00}, // 117 u
    {0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00, 0x00, 0x00}, // 118 v
    {0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00, 0x00, 0x00}, // 119 w
    {0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00}, // 120 x
    {0x1C, 0xA0, 0xA0, 0xA0, 0x7C, 0x00, 0x00, 0x00}, // 121 y
    {0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, 0x00, 0x00}, // 122 z
    {0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00}, // 123 {
    {0x00, 0x00, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00}, // 124 |
    {0x00, 0x41, 0x36, 0x08, 0x00, 0x00, 0x00, 0x00}, // 125 }
    {0x02, 0x01, 0x02, 0x04, 0x02, 0x00, 0x00, 0x00}, // 126 ~
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // 127
};

static esp_err_t oled_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (s_oled_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true); // Control byte: Co=0, D/C#=0 (Single command)
    i2c_master_write_byte(link, cmd, true);
    i2c_master_stop(link);
    esp_err_t ret = i2c_master_cmd_begin(OLED_I2C_PORT, link, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(link);
    return ret;
}

static esp_err_t oled_write_cmds(const uint8_t *cmds, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = oled_write_cmd(cmds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "oled_write_cmd failed on byte %u (0x%02X): %s",
                     (unsigned)i, cmds[i], esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

static esp_err_t oled_write_data(const uint8_t *data, size_t size)
{
    if (size == 0) return ESP_OK;
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = (size - offset > 128) ? 128 : (size - offset);
        i2c_cmd_handle_t link = i2c_cmd_link_create();
        i2c_master_start(link);
        i2c_master_write_byte(link, (s_oled_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(link, 0x40, true); // Control byte: Co=0, D/C#=1 (Data)
        i2c_master_write(link, (uint8_t *)data + offset, chunk, true);
        i2c_master_stop(link);
        esp_err_t ret = i2c_master_cmd_begin(OLED_I2C_PORT, link, pdMS_TO_TICKS(200));
        i2c_cmd_link_delete(link);
        if (ret != ESP_OK) {
            return ret;
        }
        offset += chunk;
    }
    return ESP_OK;
}

static void oled_flush_buffer(void)
{
    if (!s_initialized) return;

    if (s_i2c_mutex != NULL) {
        xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    }

    // Universal Page-by-Page Addressing (compatible with both SSD1306 and SH1106)
    for (uint8_t page = 0; page < 8; page++) {
        uint8_t page_cmds[] = {
            (uint8_t)(0xB0 + page), // Set Page Start Address (0xB0 - 0xB7)
            0x00,                   // Set Lower Column Address
            0x10                    // Set Higher Column Address
        };
        oled_write_cmds(page_cmds, sizeof(page_cmds));

        // Stream 128 bytes of pixel data for this page
        oled_write_data(&s_display_buffer[page * OLED_WIDTH], OLED_WIDTH);
    }

    if (s_i2c_mutex != NULL) {
        xSemaphoreGive(s_i2c_mutex);
    }
}

void oled_display_clear(void)
{
    memset(s_display_buffer, 0, sizeof(s_display_buffer));
    oled_flush_buffer();
}

static void oled_draw_pixel(int x, int y, bool color)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    uint16_t index = x + (y / 8) * OLED_WIDTH;
    uint8_t bit = y % 8;
    if (color) {
        s_display_buffer[index] |= (1 << bit);
    } else {
        s_display_buffer[index] &= ~(1 << bit);
    }
}

static void oled_draw_char(int x, int y, char c, bool color)
{
    if (c < 32 || c > 127) c = '?';
    const uint8_t *glyph = font8x8_basic[c - 32];
    for (int col = 0; col < 8; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 8; row++) {
            if (line & (1 << row)) {
                oled_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

static void oled_draw_string(int x, int y, const char *str, bool color)
{
    while (*str) {
        oled_draw_char(x, y, *str, color);
        x += 8;
        str++;
    }
}

static void oled_draw_hline(int x1, int x2, int y, bool color)
{
    for (int x = x1; x <= x2; x++) {
        oled_draw_pixel(x, y, color);
    }
}

static void oled_draw_rect(int x, int y, int w, int h, bool color)
{
    oled_draw_hline(x, x + w - 1, y, color);
    oled_draw_hline(x, x + w - 1, y + h - 1, color);
    for (int i = y; i < y + h; i++) {
        oled_draw_pixel(x, i, color);
        oled_draw_pixel(x + w - 1, i, color);
    }
}


esp_err_t oled_display_init(gpio_num_t sda_pin, gpio_num_t scl_pin)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing I2C Master on SDA: GPIO %d, SCL: GPIO %d (100 kHz)...", sda_pin, scl_pin);

    if (s_i2c_mutex == NULL) {
        s_i2c_mutex = xSemaphoreCreateMutex();
    }

    // Cleanly delete any existing driver before configuring
    i2c_driver_delete(OLED_I2C_PORT);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl_pin,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, // 100kHz Standard Mode for robust breadboard wiring
        .clk_flags = 0
    };

    esp_err_t err = i2c_param_config(OLED_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(OLED_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Allow I2C bus lines to stabilize with pullups
    vTaskDelay(pdMS_TO_TICKS(50));

    // Full 7-bit bus scan across 0x01 to 0x7F to reliably wake up bus and locate OLED address
    uint8_t found_addr = 0;
    for (uint8_t addr = 1; addr < 128; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, " -> [ACK] Device detected at 7-bit address 0x%02X", addr);
            if (addr == 0x3C || addr == 0x3D) {
                found_addr = addr;
                break;
            } else if (found_addr == 0) {
                found_addr = addr;
            }
        }
    }

    if (found_addr == 0) {
        ESP_LOGE(TAG, "❌ No OLED responded at 0x3C or 0x3D! Check SDA (GPIO %d) and SCL (GPIO %d) wiring, power, and pullups.",
                 sda_pin, scl_pin);
        s_initialized = false;
        return ESP_ERR_NOT_FOUND;
    }

    s_oled_addr = found_addr;
    ESP_LOGI(TAG, "✅ Target OLED I2C Address identified: 0x%02X", s_oled_addr);

    // Standard SSD1306 Display Initialization Sequence
    const uint8_t init_cmds[] = {
        0xAE,       // Display OFF
        0xD5, 0x80, // Set Display Clock Divide Ratio / Oscillator Frequency
        0xA8, 0x3F, // Set Multiplex Ratio (1 to 64) -> 64 lines
        0xD3, 0x00, // Set Display Offset (0)
        0x40,       // Set Display Start Line (0)
        0x8D, 0x14, // Enable SSD1306 Charge Pump (7.5V)
        0x20, 0x02, // Page Addressing Mode (universal)
        0xA1,       // Column Address 127 is mapped to SEG0 (Horizontally inverted for standard orientation)
        0xC8,       // Set COM Output Scan Direction (Remapped)
        0xDA, 0x12, // Set COM Pins Hardware Configuration
        0x81, 0x7F, // Stable Contrast Control (127) - Prevents charge pump overload & wobbling!
        0xD9, 0xF1, // Pre-charge Period (Phase 1 = 1 DCLK, Phase 2 = 15 DCLK - standard for internal pump)
        0xDB, 0x40, // VCOMH Deselect Level (~0.77 x VCC)
        0x2E,       // Deactivate Scroll
        0xA4,       // Entire Display ON follows RAM
        0xA6,       // Normal Display (Not Inverted)
        0xAF        // Display ON!
    };

    err = oled_write_cmds(init_cmds, sizeof(init_cmds));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306/SH1106 init commands failed: %s", esp_err_to_name(err));
        s_initialized = false;
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "SSD1306/SH1106 OLED (Addr 0x%02X) initialized successfully!", s_oled_addr);

    oled_display_show_message("ESP32 LOGGER", "Initializing...", "Sensors Booting");
    return ESP_OK;
}

esp_err_t oled_display_fill_raw(uint8_t pattern)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    memset(s_display_buffer, pattern, sizeof(s_display_buffer));
    oled_flush_buffer();
    return ESP_OK;
}

uint8_t oled_display_get_addr(void)
{
    return s_oled_addr;
}

void oled_display_force_all_on(bool force_on)
{
    if (!s_initialized) return;
    oled_write_cmd(force_on ? 0xA5 : 0xA4);
}

esp_err_t oled_display_test(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Cannot run oled_display_test: display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting visual display self-test patterns...");

    // Pattern 1: All white
    memset(s_display_buffer, 0xFF, sizeof(s_display_buffer));
    oled_flush_buffer();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Pattern 2: Checkerboard
    memset(s_display_buffer, 0xAA, sizeof(s_display_buffer));
    oled_flush_buffer();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Pattern 3: Clear and show test screen
    oled_display_show_message("DISPLAY TEST", "ALL PATTERNS OK", "ONLINE & READY");
    vTaskDelay(pdMS_TO_TICKS(1500));

    return ESP_OK;
}

void oled_display_show_message(const char *line1, const char *line2, const char *line3)
{
    memset(s_display_buffer, 0, sizeof(s_display_buffer));

    // Outer border box
    oled_draw_rect(0, 0, 128, 64, true);

    // Title banner line
    oled_draw_hline(0, 127, 18, true);

    if (line1) oled_draw_string(8, 5, line1, true);
    if (line2) oled_draw_string(8, 26, line2, true);
    if (line3) oled_draw_string(8, 44, line3, true);

    oled_flush_buffer();
}

static char s_fw_title[24] = "LOGGER v1.0 [GRN]";

void oled_display_set_fw_version(const char *version_str)
{
    if (version_str) {
        strncpy(s_fw_title, version_str, sizeof(s_fw_title) - 1);
        s_fw_title[sizeof(s_fw_title) - 1] = '\0';
    }
}

void oled_display_show_sensor_data(float temp, float hum, float pressure, bool valve_active)
{
    memset(s_display_buffer, 0, sizeof(s_display_buffer));

    // Header Title Bar with dynamic Firmware Version badge
    oled_draw_rect(0, 0, 128, 16, true);
    oled_draw_string(6, 4, s_fw_title, true);

    // Divider line
    oled_draw_hline(0, 127, 48, true);

    // Temp Line
    char buf[20];
    snprintf(buf, sizeof(buf), "TEMP: %.1f C", temp);
    oled_draw_string(4, 20, buf, true);

    // Hum Line
    snprintf(buf, sizeof(buf), "HUM : %.1f %%", hum);
    oled_draw_string(4, 34, buf, true);

    // Bottom Status Line (Valve status & Pressure)
    snprintf(buf, sizeof(buf), "V:%s P:%.1f", valve_active ? "ON " : "OFF", pressure);
    oled_draw_string(4, 52, buf, true);

    oled_flush_buffer();
}
