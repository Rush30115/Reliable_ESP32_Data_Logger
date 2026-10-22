#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "wifi_credentials.h"
#include "esp_log.h"
#include "sensor_record.h"
#include "sensor_engine.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "http_telemetry.h"
#include "system_health.h"
#include "valve_control.h"
#include "mqtt_telemetry.h"
#include "oled_display.h"
#include "display_test.h"
#include "esp_https_ota.h"                                               
#include "esp_http_client.h"                                             
#include "esp_ota_ops.h"                                                 
#include "wifi_credentials.h"
#include "esp_log.h"                                                     
#include "esp_crt_bundle.h"                      
#include "driver/gpio.h"

// =========================================================================
// Firmware Version & Status LED Configuration
// Set FIRMWARE_VERSION:
//   1 -> Boot v1.0: Blinks LED on GPIO 2 (Green LED)
//   2 -> After 1st OTA v2.0: Blinks LED on GPIO 15 (Yellow LED)
//   3 -> After 2nd OTA v3.0: Blinks LED on GPIO 4 (Blue LED)
// =========================================================================
#define FIRMWARE_VERSION 2

#if FIRMWARE_VERSION == 1
    #define ACTIVE_BLINK_PIN    GPIO_NUM_2
    #define FW_VERSION_LABEL    "v1.0 (Initial Boot - GPIO 2 GREEN LED)"
    #define FW_OLED_LABEL       "APP v1.0 [GRN]"
#elif FIRMWARE_VERSION == 2
    #define ACTIVE_BLINK_PIN    GPIO_NUM_15
    #define FW_VERSION_LABEL    "v2.0 (1st OTA - GPIO 15 YELLOW LED)"
    #define FW_OLED_LABEL       "APP v2.0 [YEL]"
#elif FIRMWARE_VERSION == 3
    #define ACTIVE_BLINK_PIN    GPIO_NUM_4
    #define FW_VERSION_LABEL    "v3.0 (2nd OTA - GPIO 4 BLUE LED)"
    #define FW_OLED_LABEL       "APP v3.0 [BLU]"
#else
    #define ACTIVE_BLINK_PIN    GPIO_NUM_2
    #define FW_VERSION_LABEL    "vUnknown"
    #define FW_OLED_LABEL       "APP vUnknown"
#endif

static const char *OTA_TAG = "LOGGER_OTA";                               
                                                                             
    // Auto-generated symbols from EMBED_TXTFILES "certs/server_cert.pem"   
extern const uint8_t server_cert_pem_start[]                             
asm("_binary_server_cert_pem_start");                                      
extern const uint8_t server_cert_pem_end[]                               
asm("_binary_server_cert_pem_end");

static const char *TAG = "MAIN_APP";

QueueHandle_t xSensorQueue = NULL;

static void led_blink_task(void *pvParameter)
{
    gpio_num_t pin = (gpio_num_t)(intptr_t)pvParameter;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "LED Blinker active on GPIO %d for %s", pin, FW_VERSION_LABEL);

    int level = 0;
    while (1) {
        gpio_set_level(pin, level);
        level = !level;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static SemaphoreHandle_t s_ota_trigger_sem = NULL;

void ota_trigger_update(void)
{
    if (s_ota_trigger_sem) {
        xSemaphoreGive(s_ota_trigger_sem);
    }
}

#define BOOT_BUTTON_GPIO    GPIO_NUM_0
#define EXT_BUTTON_GPIO     GPIO_NUM_13

static void button_trigger_task(void *pvParameter)
{
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO) | (1ULL << EXT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    while (1) {
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0 || gpio_get_level(EXT_BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            if (gpio_get_level(BOOT_BUTTON_GPIO) == 0 || gpio_get_level(EXT_BUTTON_GPIO) == 0) {
                ESP_LOGI(TAG, "🔘 Trigger Button Pressed! Triggering OTA update on-demand...");
                ota_trigger_update();
                while (gpio_get_level(BOOT_BUTTON_GPIO) == 0 || gpio_get_level(EXT_BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#define LOCAL_OTA_URL_LAN   "https://10.25.172.80:8070/firmware.bin"
#define LOCAL_OTA_URL_WOKWI "https://10.0.2.2:8070/firmware.bin"

void ota_task(void *pvParameter)                                         
{                                                                        
    const char *firmware_urls[] = {
        LOCAL_OTA_URL_LAN,
        LOCAL_OTA_URL_WOKWI
    };
    const int num_urls = sizeof(firmware_urls) / sizeof(firmware_urls[0]);

    ESP_LOGI(OTA_TAG, "=======================================================");
    ESP_LOGI(OTA_TAG, "🚀 AUTOMATIC OTA BACKGROUND MONITOR ACTIVE!");
    ESP_LOGI(OTA_TAG, "👉 The ESP32 checks for new firmware every 10s.");
    ESP_LOGI(OTA_TAG, "👉 Placing a new 'firmware.bin' on server triggers auto-upgrade & auto-reboot!");
    ESP_LOGI(OTA_TAG, "=======================================================");

    while (1) {
        // Automatically check every 10 seconds, or immediately if button pressed
        xSemaphoreTake(s_ota_trigger_sem, pdMS_TO_TICKS(10000));

        // Ensure Wi-Fi is connected before attempting connection
        if (!wifi_manager_is_connected()) {
            continue;
        }

        bool update_done = false;
        for (int i = 0; i < num_urls; i++) {
            const char *firmware_url = firmware_urls[i];

            esp_http_client_config_t http_config = {
                .url = firmware_url,
                .timeout_ms = 6000,
                .keep_alive_enable = true,
            };

            if (strstr(firmware_url, "10.") || strstr(firmware_url, "192.168.") || strstr(firmware_url, "127.")) {
                http_config.cert_pem = (const char *)server_cert_pem_start;
                http_config.skip_cert_common_name_check = true;
            } else {
                http_config.crt_bundle_attach = esp_crt_bundle_attach;
            }

            esp_https_ota_config_t ota_config = {
                .http_config = &http_config,
            };

            esp_https_ota_handle_t https_ota_handle = NULL;
            esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
            if (err == ESP_OK) {
                esp_app_desc_t new_app_desc;
                const esp_app_desc_t *running_app_desc = esp_app_get_description();

                if (esp_https_ota_get_img_desc(https_ota_handle, &new_app_desc) == ESP_OK) {
                    if (memcmp(new_app_desc.app_elf_sha256, running_app_desc->app_elf_sha256, sizeof(new_app_desc.app_elf_sha256)) == 0) {
                        ESP_LOGI(OTA_TAG, "Server firmware matches active binary (%s). System is up-to-date! Skipping OTA.", firmware_url);
                        esp_https_ota_abort(https_ota_handle);
                        update_done = true;
                        break;
                    }
                }

                ESP_LOGI(OTA_TAG, "⚡ NEW FIRMWARE DETECTED ON SERVER! Initiating automatic upgrade...");
                oled_display_show_message("OTA DETECTED!", "STARTING FLASH", "DOWNLOADING...");
                int last_logged_percent = -1;

                while (1) {
                    err = esp_https_ota_perform(https_ota_handle);
                    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                        break;
                    }
                    int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
                    int total_len = esp_https_ota_get_image_size(https_ota_handle);
                    int percent = (total_len > 0) ? (read_len * 100 / total_len) : 0;
                    if (percent % 20 == 0 && percent != last_logged_percent) {
                        ESP_LOGI(OTA_TAG, "OTA Progress: %d%% (%d / %d bytes)", percent, read_len, total_len);
                        char pbuf[24];
                        snprintf(pbuf, sizeof(pbuf), "FLASHING: %d%%", percent);
                        oled_display_show_message("OTA FLASHING", pbuf, "PLEASE WAIT...");
                        last_logged_percent = percent;
                    }
                    vTaskDelay(pdMS_TO_TICKS(20));
                }

                if (esp_https_ota_is_complete_data_received(https_ota_handle) == true) {
                    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
                    if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
                        ESP_LOGI(OTA_TAG, "OTA Download & Flash Complete! Auto-rebooting into upgraded version in 2s...");
                        oled_display_show_message("OTA COMPLETE!", "REBOOTING...", "BOOTING NEW SLOT");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        esp_restart();
                    } else {
                        ESP_LOGE(OTA_TAG, "OTA finish failed (%s)", esp_err_to_name(ota_finish_err));
                        oled_display_show_message("OTA ERROR", "FINISH FAILED", "TRY AGAIN");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    }
                } else {
                    ESP_LOGW(OTA_TAG, "Incomplete data received. Aborting OTA session.");
                    oled_display_show_message("OTA ABORTED", "INCOMPLETE DATA", "TRY AGAIN");
                    esp_https_ota_abort(https_ota_handle);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
                update_done = true;
                break;
            } else {
                // Endpoint probe failed, will try next url in loop
            }
        }

        if (!update_done) {
            ESP_LOGD(OTA_TAG, "No OTA server endpoint reached on port 8070. Retrying in 10s...");
        }
    }
}

#define DEFAULT_OTA_URL  LOCAL_OTA_URL_LAN

void app_main(void)
{
    // 0. Check running partition and log active firmware version
    const esp_partition_t *running = esp_ota_get_running_partition();

    ESP_LOGI(TAG, "=================================================================");
    ESP_LOGI(TAG, "   🔥 FIRMWARE %s ACTIVE 🔥", FW_VERSION_LABEL);
    ESP_LOGI(TAG, "   💡 Blinking Status LED on GPIO %d                             ", ACTIVE_BLINK_PIN);
    ESP_LOGI(TAG, "   📟 OLED Status Badge: %s                                     ", FW_OLED_LABEL);
    ESP_LOGI(TAG, "=================================================================");

    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "Active Running Partition: %s (Offset: 0x%08lX)", 
             running->label, (unsigned long)running->address);
    ESP_LOGI(TAG, "--------------------------------------------------");

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(OTA_TAG, "Freshly upgraded image detected! Validating firmware to cancel rollback...");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // 1. Initialize Task Watchdog Timer Supervision (15s timeout)
    ESP_ERROR_CHECK(system_health_init(15));

    // 2. Create Inter-Task Communication Queue (Depth: 32 Sensor Records)
    xSensorQueue = xQueueCreate(32, sizeof(sensor_record_t));
    configASSERT(xSensorQueue != NULL);

    // 3. Initialize Storage Subsystem (NVS & LittleFS Store-and-Forward Engine)
    ESP_ERROR_CHECK(storage_engine_init());

    // 4. Initialize Valve Control Subsystem (GPIO 12, Threshold 30.0)
    ESP_ERROR_CHECK(valve_control_init(DEFAULT_VALVE_GPIO_PIN, DEFAULT_PRESSURE_THRESHOLD));

    // 5. Initialize Hardware Sensor Engine & Run OLED Display Diagnostic Test Suite
    ESP_LOGI(TAG, "Running OLED Display Hardware Diagnostic Test Suite...");
    display_test_report_t display_report;
    esp_err_t oled_ret = display_test_run(OLED_SDA_PIN, OLED_SCL_PIN, &display_report);
    if (oled_ret == ESP_OK) {
        ESP_LOGI(TAG, "OLED Hardware Diagnostic PASSED! Address: 0x%02X", display_report.oled_addr);
        oled_display_set_fw_version(FW_OLED_LABEL);
    } else {
        ESP_LOGE(TAG, "OLED Hardware Diagnostic FAILED (%s): %s",
                 esp_err_to_name(oled_ret), display_report.diag_message);
        ESP_LOGW(TAG, "Continuing boot sequence. Check physical wiring / pins / address.");
    }
    ESP_ERROR_CHECK(sensor_engine_init(SENSOR_GPIO_PIN));

    // 6. Initialize Network Manager (Hardware Wi-Fi AP with fallback)
    ESP_ERROR_CHECK(wifi_manager_init(WIFI_SSID, WIFI_PASS));

    // 7. Initialize Telemetry Engine (AWS IoT Core mTLS MQTT Client)
    ESP_ERROR_CHECK(mqtt_telemetry_init(AWS_IOT_BROKER_URI, AWS_IOT_CLIENT_ID));

    // 8. Spawn Multi-Tasking Architecture across ESP32 Dual Cores
    // LED Blinker Task (Core 1, Priority 1) - Blinks designated Pin (Pin 2 / 3 / 4)
    xTaskCreatePinnedToCore(
        led_blink_task,
        "led_blink_task",
        2048,
        (void *)(intptr_t)ACTIVE_BLINK_PIN,
        1,
        NULL,
        1
    );

    // Sensor Engine Task (Core 1, Priority 5)
    xTaskCreatePinnedToCore(
        sensor_engine_task,
        "sensor_task",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    // Storage Engine Task (Core 0, Priority 4)
    xTaskCreatePinnedToCore(
        storage_engine_task,
        "storage_task",
        4096,
        NULL,
        4,
        NULL,
        0
    );

    // HTTP Telemetry Task (Core 0, Priority 3)
    xTaskCreatePinnedToCore(
        http_telemetry_task,
        "telemetry_task",
        6144,
        NULL,
        3,
        NULL,
        0
    );

    // MQTT Bi-Directional Telemetry & Threshold Task (Core 0, Priority 3)
    xTaskCreatePinnedToCore(
        mqtt_telemetry_task,
        "mqtt_task",
        6144,
        NULL,
        3,
        NULL,
        0
    );

    // System Health Diagnostics Task (Core 0, Priority 2)
    xTaskCreatePinnedToCore(
        system_health_task,
        "sys_health_task",
        3072,
        NULL,
        2,
        NULL,
        0
    );

    // 9. On-Demand OTA Trigger Infrastructure
    s_ota_trigger_sem = xSemaphoreCreateBinary();
    configASSERT(s_ota_trigger_sem != NULL);

    // Button Monitor Task (GPIO 0 BOOT Button & GPIO 13 Pushbutton)
    xTaskCreatePinnedToCore(
        button_trigger_task,
        "btn_trigger_task",
        2048,
        NULL,
        2,
        NULL,
        1
    );

    // On-Demand OTA Firmware Update Task (Core 0, Priority 1)
    xTaskCreatePinnedToCore(ota_task, "ota_task", 8192, NULL, 1, NULL, 0);

    ESP_LOGI(TAG, "All FreeRTOS tasks and hardware subsystems initialized successfully.");
}
