/**
 * @file wifi_manager.c
 * @brief WiFi connectivity management – implementation.
 */

#include "wifi_manager.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_manager";

/* ── Internal state ───────────────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t    s_wifi_event_group = NULL;
static wifi_manager_state_cb_t s_state_cb       = NULL;
static void                 *s_state_cb_ctx      = NULL;
static bool                  s_connected         = false;
static char                  s_ip_str[16]        = {0};
static uint8_t               s_retry_num         = 0;
static uint8_t               s_max_retry         = WIFI_MANAGER_DEFAULT_MAX_RETRY;

/* ── Event handler ────────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        memset(s_ip_str, 0, sizeof(s_ip_str));

        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry %d/%d …", s_retry_num, s_max_retry);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after %d retries", s_max_retry);
            if (s_state_cb) s_state_cb(false, NULL, s_state_cb_ctx);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR,
                 IP2STR(&event->ip_info.ip));

        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        ESP_LOGI(TAG, "Connected — IP: %s", s_ip_str);
        if (s_state_cb) s_state_cb(true, s_ip_str, s_state_cb_ctx);
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t wifi_manager_init(const wifi_manager_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    s_max_retry = (cfg->max_retry > 0)
                      ? cfg->max_retry
                      : WIFI_MANAGER_DEFAULT_MAX_RETRY;
    s_retry_num = 0;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,  &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     cfg->ssid,
            sizeof(wifi_cfg.sta.ssid)     - 1);
    strncpy((char *)wifi_cfg.sta.password, cfg->password,
            sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s", cfg->ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;

    return ESP_ERR_TIMEOUT;
}

void wifi_manager_deinit(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    s_connected = false;
}

void wifi_manager_register_state_cb(wifi_manager_state_cb_t cb, void *ctx)
{
    s_state_cb     = cb;
    s_state_cb_ctx = ctx;
}

bool wifi_manager_is_connected(void) { return s_connected; }

esp_err_t wifi_manager_get_ip(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 16) return ESP_ERR_INVALID_ARG;
    if (!s_connected)         return ESP_ERR_INVALID_STATE;
    strncpy(buf, s_ip_str, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return ESP_OK;
}