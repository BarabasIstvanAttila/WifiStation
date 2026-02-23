/**
 * @file app_main.c
 * @brief Application entry point — orchestration only.
 *
 * Normal wake cycle (production)
 * ──────────────────────────────
 * Runs from scratch on every timer wake from deep sleep.
 *
 *  0. NVS init         — required by the WiFi driver.
 *  1. Power management — configure DFS before any driver starts.
 *  2. Camera init      — hardware only, no network needed.
 *  3. WiFi connect     — block until IP obtained or retries exhausted.
 *  4. SNTP sync        — set wall-clock time; skip upload if sync fails.
 *  5. Schedule check   — is today within the last N days of the month?
 *                        Has today's upload already been done (RTC guard)?
 *       IN WINDOW + not yet done → capture (with flash) → upload → mark done.
 *       Otherwise                → skip.
 *  6. Teardown         — stop SNTP, disconnect WiFi, deinit camera.
 *  7. Deep sleep       — until tomorrow at APP_DAILY_CHECK_HOUR:MINUTE.
 *
 * Test mode (CONFIG_APP_TEST_UPLOAD_MODE=y)
 * ─────────────────────────────────────────
 * Steps 0–4 are identical.  Step 5 bypasses all scheduling: an upload is
 * always attempted.  mark_uploaded() is never called.  Step 7 sleeps for
 * APP_TEST_UPLOAD_INTERVAL_S seconds (default 60) instead of until tomorrow.
 * A WARNING is printed at every boot when test mode is active.
 */

#include "app_config.h"
#include "camera_hal.h"
#include "image_capture.h"
#include "wifi_manager.h"
#include "sntp_sync.h"
#include "image_uploader.h"
#include "upload_scheduler.h"

#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "app_main";

/* ── WiFi state callback ──────────────────────────────────────────────────── */

static void on_wifi_state_change(bool connected, const char *ip, void *ctx)
{
    (void)ctx;
    if (connected) {
        ESP_LOGI(TAG, "Network ready — IP: %s", ip);
    } else {
        ESP_LOGW(TAG, "Network lost");
    }
}

/* ── Upload pipeline ──────────────────────────────────────────────────────── */

/*
 * Capture one frame (with flash if the board supports it) and stream it to
 * the configured endpoint.  Called once per qualifying wake cycle.
 */
static esp_err_t run_daily_upload(void)
{
    ESP_LOGI(TAG, "=== Daily upload starting ===");

    /* Step 1: Acquire frame.  image_capture handles flash on/warmup/off. */
    capture_frame_t frame;
    esp_err_t err = image_capture_acquire_frame(&frame);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Frame capture failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Frame #%"PRIu32" captured: %zu bytes",
             frame.sequence, frame.fb->len);

    /* Step 2: Configure the uploader from Kconfig values. */
    image_uploader_config_t up_cfg = {
        .method          = APP_UPLOAD_METHOD,
        .chunk_size      = APP_UPLOAD_CHUNK_SIZE,
        .timeout_ms      = APP_UPLOAD_TIMEOUT_MS,
        .retry_count     = APP_UPLOAD_RETRY_COUNT,
        .skip_tls_verify = APP_UPLOAD_SKIP_TLS_VERIFY,
    };
    strncpy(up_cfg.endpoint_url, APP_UPLOAD_ENDPOINT_URL,
            sizeof(up_cfg.endpoint_url) - 1);
    strncpy(up_cfg.username, APP_UPLOAD_USERNAME,
            sizeof(up_cfg.username) - 1);
    strncpy(up_cfg.password, APP_UPLOAD_PASSWORD,
            sizeof(up_cfg.password) - 1);

    err = image_uploader_init(&up_cfg);
    if (err != ESP_OK) {
        image_capture_return_frame(&frame);
        ESP_LOGE(TAG, "Uploader init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Step 3: Stream the frame to the endpoint. */
    upload_result_t result;
    err = image_uploader_send(&frame, &result);

    /* Always return the frame buffer — even on upload failure. */
    image_capture_return_frame(&frame);
    image_uploader_deinit();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Upload succeeded: HTTP %d, %"PRIu32" bytes, %d attempt(s)",
                 result.http_status, result.bytes_sent, result.attempts);
        upload_scheduler_mark_uploaded();
    } else {
        ESP_LOGE(TAG, "Upload failed after %d attempt(s) — will retry next wake",
                 result.attempts);
        /*
         * Do NOT mark_uploaded() — the RTC flag stays clear so the next daily
         * wake (within the same window day) can try again after a reboot,
         * or tomorrow's wake will try again on the next window day.
         */
    }

    return err;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void app_main(void)
{
    /* ── 0. NVS ─────────────────────────────────────────────────────────── */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

/*
 * Test mode banner — printed at every boot so it is impossible to forget
 * that a development build is running.  Compiles away completely in
 * production builds (CONFIG_APP_TEST_UPLOAD_MODE not set).
 */
#ifdef CONFIG_APP_TEST_UPLOAD_MODE
    ESP_LOGW(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGW(TAG, "║   *** TEST UPLOAD MODE ENABLED ***   ║");
    ESP_LOGW(TAG, "║  Uploading every %3ds — NOT for     ║",
             APP_TEST_UPLOAD_INTERVAL_S);
    ESP_LOGW(TAG, "║  production use.  Disable in        ║");
    ESP_LOGW(TAG, "║  menuconfig → Test Mode.            ║");
    ESP_LOGW(TAG, "╚══════════════════════════════════════╝");
#endif

    /* ── 1. Power management ─────────────────────────────────────────────── */
#ifdef CONFIG_PM_ENABLE
    {
        esp_pm_config_t pm_cfg = {
            .max_freq_mhz       = APP_MAX_CPU_FREQ_MHZ,
            .min_freq_mhz       = APP_MIN_CPU_FREQ_MHZ,
            .light_sleep_enable = true,
        };
        ESP_ERROR_CHECK(esp_pm_configure(&pm_cfg));
        ESP_LOGI(TAG, "PM: DFS %d–%d MHz", APP_MIN_CPU_FREQ_MHZ, APP_MAX_CPU_FREQ_MHZ);
    }
#endif

    /* ── 2. Camera ───────────────────────────────────────────────────────── */
    camera_hal_config_t cam_cfg = {
        .pixel_format = APP_CAMERA_FORMAT,
        .frame_size   = APP_CAMERA_FRAMESIZE,
        .jpeg_quality = APP_CAMERA_QUALITY,
        .fb_count     = APP_CAMERA_FB_COUNT,
    };
    ESP_ERROR_CHECK(camera_hal_config_for_board(&cam_cfg, APP_CAMERA_BOARD));
    ESP_ERROR_CHECK(camera_hal_init(&cam_cfg));

    /* ── 3. WiFi ─────────────────────────────────────────────────────────── */
    wifi_manager_register_state_cb(on_wifi_state_change, NULL);

    wifi_manager_config_t wifi_cfg = { .max_retry = APP_WIFI_MAX_RETRY };
    strncpy(wifi_cfg.ssid,     APP_WIFI_SSID,     sizeof(wifi_cfg.ssid)     - 1);
    strncpy(wifi_cfg.password, APP_WIFI_PASSWORD, sizeof(wifi_cfg.password) - 1);

    bool wifi_ok = (wifi_manager_init(&wifi_cfg) == ESP_OK);
    if (wifi_ok) {
        ESP_ERROR_CHECK(esp_wifi_set_ps(APP_WIFI_PS_MODE));
    } else {
        ESP_LOGE(TAG, "WiFi failed — upload skipped this cycle");
    }

    /* ── 4. SNTP ─────────────────────────────────────────────────────────── */
    bool time_ok = false;
    if (wifi_ok) {
        sntp_sync_config_t sntp_cfg = { .sync_timeout_ms = APP_SNTP_TIMEOUT_MS };
        strncpy(sntp_cfg.server,   APP_SNTP_SERVER, sizeof(sntp_cfg.server)   - 1);
        strncpy(sntp_cfg.tz_posix, APP_TIMEZONE,    sizeof(sntp_cfg.tz_posix) - 1);

        ESP_ERROR_CHECK(sntp_sync_init(&sntp_cfg));
        time_ok = (sntp_sync_wait(APP_SNTP_TIMEOUT_MS) == ESP_OK);

        if (!time_ok) {
            ESP_LOGE(TAG, "SNTP sync failed — upload skipped this cycle");
        }
    }

    /* ── 5. Schedule check and upload ────────────────────────────────────── */
    if (wifi_ok && time_ok) {

#ifdef CONFIG_APP_TEST_UPLOAD_MODE
        /*
         * TEST MODE: ignore the upload window and the per-day deduplication
         * guard entirely.  Always attempt an upload.  Never call
         * mark_uploaded() so the flag stays clear and the next wake
         * (APP_TEST_UPLOAD_INTERVAL_S seconds away) will upload again.
         */
        ESP_LOGW(TAG, "TEST MODE: skipping schedule check — uploading now");
        run_daily_upload();

#else   /* production */

        bool in_window    = upload_scheduler_is_within_upload_window(
                                APP_UPLOAD_WINDOW_DAYS);
        bool already_done = upload_scheduler_already_uploaded_today();

        if (in_window && !already_done) {
            run_daily_upload();
        } else if (already_done) {
            ESP_LOGI(TAG, "Already uploaded today — standby until tomorrow");
        } else {
            ESP_LOGI(TAG, "Not within upload window — standby");
        }

#endif  /* CONFIG_APP_TEST_UPLOAD_MODE */
    }

    /* ── 6. Teardown ─────────────────────────────────────────────────────── */
    if (wifi_ok) {
        sntp_sync_deinit();
        wifi_manager_deinit();
    }
    camera_hal_deinit();

    ESP_LOGI(TAG, "Free heap before sleep: %"PRIu32" bytes",
             esp_get_free_heap_size());

    /* ── 7. Deep sleep ───────────────────────────────────────────────────── */
#ifdef CONFIG_APP_TEST_UPLOAD_MODE
    /*
     * TEST MODE: sleep for a fixed short interval then wake and upload again.
     * No scheduler config needed — just a raw second count.
     */
    ESP_LOGW(TAG, "TEST MODE: sleeping %d s before next upload",
             APP_TEST_UPLOAD_INTERVAL_S);
    upload_scheduler_deep_sleep((uint64_t)APP_TEST_UPLOAD_INTERVAL_S);

#else   /* production */

    const upload_scheduler_config_t sched_cfg = {
        .check_hour         = APP_DAILY_CHECK_HOUR,
        .check_minute       = APP_DAILY_CHECK_MINUTE,
        .upload_window_days = APP_UPLOAD_WINDOW_DAYS,
    };

    uint64_t sleep_secs = time_ok
        ? upload_scheduler_seconds_until_next_check(&sched_cfg)
        : 86400; /* fallback: 24 hours when time is unknown */

    upload_scheduler_deep_sleep(sleep_secs);

#endif  /* CONFIG_APP_TEST_UPLOAD_MODE */
    /* upload_scheduler_deep_sleep() never returns. */
}