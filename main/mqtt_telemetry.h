#ifndef MQTT_TELEMETRY_H
#define MQTT_TELEMETRY_H

#include "esp_err.h"
#include "sensor_record.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// AWS IoT Core Configuration (ap-southeast-2)
// ==========================================
#define AWS_IOT_ENDPOINT        "a356plyaavkv83-ats.iot.ap-southeast-2.amazonaws.com"
#define AWS_IOT_BROKER_URI      "mqtts://" AWS_IOT_ENDPOINT ":8883"
#define AWS_IOT_CLIENT_ID       "reliable-data-logger-01"

// Topic Architecture
#define AWS_IOT_TOPIC_TELEMETRY "devices/reliable-data-logger-01/telemetry"
#define AWS_IOT_TOPIC_COMMANDS  "devices/reliable-data-logger-01/commands/#"
#define AWS_IOT_TOPIC_STATUS    "devices/reliable-data-logger-01/status"

/**
 * @brief Initialize AWS IoT Core MQTT telemetry client using mTLS.
 */
esp_err_t mqtt_telemetry_init(const char *broker_uri, const char *client_id);

/**
 * @brief Check if connection to AWS IoT Core is active.
 */
bool mqtt_telemetry_is_connected(void);

/**
 * @brief Publish a single sensor record to AWS IoT Core over mTLS.
 */
esp_err_t mqtt_telemetry_publish_record(const sensor_record_t *record);

/**
 * @brief FreeRTOS task body for telemetry management & offline store-and-forward backlog draining.
 */
void mqtt_telemetry_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // MQTT_TELEMETRY_H
