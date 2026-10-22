#include "wifi_manager.h"
#include "wifi_credentials.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

static wifi_state_t s_wifi_state = WIFI_STATE_DISCONNECTED;
static uint32_t s_retry_num = 0;
static bool s_is_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Station started. Connecting to AP...");
        s_wifi_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        s_wifi_state = WIFI_STATE_CONNECTING;
        s_retry_num++;
        if (s_retry_num == 3) {
            wifi_config_t cur_conf;
            if (esp_wifi_get_config(WIFI_IF_STA, &cur_conf) == ESP_OK) {
                if (strcmp((char *)cur_conf.sta.ssid, "Wokwi-GUEST") == 0) {
                    strncpy((char *)cur_conf.sta.ssid, WIFI_SSID, sizeof(cur_conf.sta.ssid) - 1);
                    strncpy((char *)cur_conf.sta.password, WIFI_PASS, sizeof(cur_conf.sta.password) - 1);
                    cur_conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                    esp_wifi_set_config(WIFI_IF_STA, &cur_conf);
                    ESP_LOGW(TAG, "AP Wokwi-GUEST not reachable. Switched to hardware AP: %s", WIFI_SSID);
                } else {
                    strncpy((char *)cur_conf.sta.ssid, "Wokwi-GUEST", sizeof(cur_conf.sta.ssid) - 1);
                    cur_conf.sta.password[0] = '\0';
                    cur_conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
                    esp_wifi_set_config(WIFI_IF_STA, &cur_conf);
                    ESP_LOGW(TAG, "Hardware AP not reachable. Switched to virtual AP: Wokwi-GUEST");
                }
            }
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting to AP (retry %lu)...", (unsigned long)s_retry_num);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi Connected! IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        s_wifi_state = WIFI_STATE_CONNECTED;
    }
}

esp_err_t wifi_manager_init(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi Manager...");

    esp_err_t netif_ret = esp_netif_init();
    if (netif_ret != ESP_OK && netif_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(netif_ret);
    }

    esp_err_t evt_ret = esp_event_loop_create_default();
    if (evt_ret != ESP_OK && evt_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(evt_ret);
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = { 0 };
    if (ssid && strlen(ssid) > 0) {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    } else {
        strncpy((char *)wifi_config.sta.ssid, "Wokwi-GUEST", sizeof(wifi_config.sta.ssid) - 1);
    }

    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_wifi_state;
}
