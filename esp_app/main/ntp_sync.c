/**
 * @file ntp_sync.c
 * @brief NTP wall-clock synchronisation — implementation.
 *
 * See ntp_sync.h for the naming rationale (avoids sntp_sync_* collision
 * with the ESP-IDF lwIP SNTP subsystem).
 */

#include "ntp_sync.h"

#include <string.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ntp_sync";

/* ── Internal state ───────────────────────────────────────────────────────── */

static volatile bool s_synced = false;

/* ── SNTP callback ────────────────────────────────────────────────────────── */

/*
 * Called by the lwIP SNTP task when a valid response arrives.
 * Set the flag only — no allocations, no logging from this context.
 */
static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t ntp_sync_init(const ntp_sync_config_t *cfg)
{
    if (!cfg || cfg->server[0] == '\0') return ESP_ERR_INVALID_ARG;

    s_synced = false;

    /* Apply timezone before the first localtime() call. */
    setenv("TZ", cfg->tz_posix[0] ? cfg->tz_posix : "UTC", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, cfg->server);
    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();

    ESP_LOGI(TAG, "NTP started: server=%s tz=%s",
             cfg->server,
             cfg->tz_posix[0] ? cfg->tz_posix : "UTC");
    return ESP_OK;
}

esp_err_t ntp_sync_wait(uint32_t timeout_ms)
{
    const uint32_t POLL_MS = 100;
    uint32_t       elapsed = 0;

    while (!s_synced && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        elapsed += POLL_MS;
    }

    if (!s_synced) {
        ESP_LOGE(TAG, "NTP sync timed out after %" PRIu32 " ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    time_t    now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d (local)",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return ESP_OK;
}

bool ntp_sync_is_done(void) { return s_synced; }

void ntp_sync_deinit(void)
{
    esp_sntp_stop();
    s_synced = false;
    ESP_LOGD(TAG, "NTP stopped");
}