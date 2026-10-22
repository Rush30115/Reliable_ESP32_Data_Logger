#include "mqtt_telemetry.h"
#include "storage_engine.h"
#include "wifi_manager.h"
#include "valve_control.h"
#include "sensor_engine.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "AWS_IOT_MQTT";

// Binary symbols embedded via CMake EMBED_TXTFILES
extern const uint8_t root_ca_pem_start[]   asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_key_pem_start[]  asm("_binary_device_private_key_start");

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_is_mqtt_connected = false;
static char s_pub_topic[128] = AWS_IOT_TOPIC_TELEMETRY;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "🔒 Successfully Connected to AWS IoT Core (mTLS Secured)!");
        s_is_mqtt_connected = true;

        // Subscribe to device commands topic namespace
        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, AWS_IOT_TOPIC_COMMANDS, 1);
        ESP_LOGI(TAG, "Subscribed to AWS IoT Command Topic: '%s' (msg_id=%d)", AWS_IOT_TOPIC_COMMANDS, msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from AWS IoT Core");
        s_is_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "📩 AWS IoT Downlink Message | Topic: %.*s | Payload: %.*s",
                 event->topic_len, event->topic, event->data_len, event->data);

        char topic_buf[128] = {0};
        char payload_buf[256] = {0};
        int t_len = (event->topic_len < (int)sizeof(topic_buf) - 1) ? event->topic_len : (int)sizeof(topic_buf) - 1;
        int p_len = (event->data_len < (int)sizeof(payload_buf) - 1) ? event->data_len : (int)sizeof(payload_buf) - 1;
        memcpy(topic_buf, event->topic, t_len);
        memcpy(payload_buf, event->data, p_len);

        // Command processing: Dynamic Pressure Threshold adjustment
        if (strstr(topic_buf, "threshold") != NULL || strstr(payload_buf, "threshold") != NULL) {
            float new_thresh = 0.0f;
            bool valid = false;

            cJSON *json = cJSON_Parse(payload_buf);
            if (json) {
                cJSON *item = cJSON_GetObjectItem(json, "threshold");
                if (!item) item = cJSON_GetObjectItem(json, "value");
                if (item && cJSON_IsNumber(item)) {
                    new_thresh = (float)item->valuedouble;
                    valid = true;
                }
                cJSON_Delete(json);
            }

            if (!valid) {
                char *endptr = NULL;
                float parsed = strtof(payload_buf, &endptr);
                if (endptr != payload_buf) {
                    new_thresh = parsed;
                    valid = true;
                }
            }

            if (valid) {
                ESP_LOGI(TAG, "⚡ Dynamic Threshold Updated via AWS IoT: %.2f bar", new_thresh);
                valve_control_set_threshold(new_thresh);

                float cur_p = sensor_engine_get_last_pressure();
                bool valve_on = valve_control_evaluate(cur_p);
                ESP_LOGI(TAG, "Threshold Applied: Current Pressure %.2f vs Threshold %.2f -> Valve %s",
                         cur_p, new_thresh, valve_on ? "HIGH (OPEN)" : "LOW (CLOSED)");
            } else {
                ESP_LOGW(TAG, "Failed to parse threshold from AWS IoT payload: '%s'", payload_buf);
            }
        }

        // Command processing: Over-The-Air (OTA) update trigger
        if (strstr(topic_buf, "ota") != NULL || strstr(payload_buf, "ota") != NULL) {
            ESP_LOGI(TAG, "⚡ Remote AWS IoT OTA Command Received! Triggering update...");
            extern void ota_trigger_update(void);
            ota_trigger_update();
        }
        break;
    }

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "🟢 [PUBACK RECEIVED] AWS IoT Confirmed Delivery! (msg_id=%d)", event->msg_id);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "AWS IoT MQTT Event Error encountered (type=%d)", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

esp_err_t mqtt_telemetry_init(const char *broker_uri, const char *client_id)
{
    const char *uri = broker_uri ? broker_uri : AWS_IOT_BROKER_URI;
    const char *cli_id = client_id ? client_id : AWS_IOT_CLIENT_ID;

    snprintf(s_pub_topic, sizeof(s_pub_topic), "devices/%s/telemetry", cli_id);
    ESP_LOGI(TAG, "Initializing AWS IoT MQTT Client (mTLS)\n - Endpoint: %s\n - ClientID: %s\n - Pub Topic: %s",
             uri, cli_id, s_pub_topic);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = uri,
            },
            .verification = {
                .certificate = (const char *)root_ca_pem_start,
            },
        },
        .credentials = {
            .client_id = cli_id,
            .authentication = {
                .certificate = (const char *)device_cert_pem_start,
                .key = (const char *)device_key_pem_start,
            },
        },
        .session = {
            .keepalive = 60,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize ESP-IDF MQTT client for AWS IoT");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    return ESP_OK;
}

bool mqtt_telemetry_is_connected(void)
{
    return s_is_mqtt_connected;
}

esp_err_t mqtt_telemetry_publish_record(const sensor_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    uint64_t timestamp_ms = record->timestamp_us / 1000;

    // Standard AWS IoT Core Flat Telemetry Schema
    cJSON_AddStringToObject(root, "device_id", AWS_IOT_CLIENT_ID);
    cJSON_AddNumberToObject(root, "record_id", (double)record->record_id);
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)timestamp_ms);
    cJSON_AddNumberToObject(root, "pressure", record->pressure);
    cJSON_AddNumberToObject(root, "temperature", record->temperature);
    cJSON_AddNumberToObject(root, "humidity", record->humidity);
    cJSON_AddNumberToObject(root, "battery_mv", record->battery_mv);
    cJSON_AddNumberToObject(root, "valve_state", valve_control_get_state() ? 1 : 0);
    cJSON_AddNumberToObject(root, "threshold", valve_control_get_threshold());

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) return ESP_ERR_NO_MEM;

    if (s_is_mqtt_connected && s_mqtt_client) {
        // QoS 1: Guarantees delivery to AWS IoT Core with PUBACK
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_pub_topic, json_string, 0, 1, 0);
        ESP_LOGI(TAG, "Published to AWS IoT '%s' (msg_id=%d): %s",
                 s_pub_topic, msg_id, json_string);
        free(json_string);
        return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
    }

    free(json_string);
    return ESP_ERR_INVALID_STATE;
}

void mqtt_telemetry_task(void *pvParameters)
{
    ESP_LOGI(TAG, "AWS IoT Telemetry Task running on Core %d", xPortGetCoreID());
    esp_task_wdt_add(NULL);

    while (1) {
        // Offline Store-and-Forward backlog draining
        if (s_is_mqtt_connected && storage_get_pending_count() > 0) {
            ESP_LOGI(TAG, "AWS IoT Active: Draining %lu offline backlogged records from LittleFS...",
                     storage_get_pending_count());

            sensor_record_t backlog_record;
            uint32_t drained_count = 0;

            while (s_is_mqtt_connected && storage_read_oldest_record(&backlog_record) == ESP_OK) {
                if (mqtt_telemetry_publish_record(&backlog_record) == ESP_OK) {
                    storage_commit_tail_advance();
                    drained_count++;
                    vTaskDelay(pdMS_TO_TICKS(50)); // Yield to prevent network congestion
                } else {
                    ESP_LOGW(TAG, "Failed to publish backlog record #%lu to AWS IoT, pausing drain", backlog_record.record_id);
                    break;
                }
            }

            if (drained_count > 0) {
                ESP_LOGI(TAG, "Successfully drained %lu offline records to AWS IoT Core", drained_count);
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
