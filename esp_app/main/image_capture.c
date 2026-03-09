/**
 * @file image_capture.c
 * @brief Image capture implementation.
 *
 * Flash capture sequence
 * ──────────────────────
 * When the board has a flash LED (camera_hal_has_flash() == true):
 *  1. Flash ON.
 *  2. Discard the first frame immediately — the OV2640 AE/AWB algorithms
 *     need at least one full frame period to adapt to the new light level.
 *  3. Wait CONFIG_APP_CAMERA_FLASH_WARMUP_MS for the sensor to stabilise.
 *  4. Capture the actual frame.
 *  5. Flash OFF.
 *
 * When there is no flash the discard and warmup steps are skipped.
 */

#include "image_capture.h"
#include "camera_hal.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "image_capture";

/* ── Module state ─────────────────────────────────────────────────────────── */

static uint32_t s_frame_count = 0;

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/*
 * Discard one frame from the driver pool without exposing it to the caller.
 * Used to let the sensor's AE/AWB settle after the flash is switched on.
 */
static void discard_one_frame(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t image_capture_acquire_frame(capture_frame_t *out_frame)
{
    if (!out_frame) return ESP_ERR_INVALID_ARG;

    if (!camera_hal_is_ready()) {
        ESP_LOGE(TAG, "Camera not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    bool using_flash = camera_hal_has_flash();

    if (using_flash) {
        /*
         * Step 1: Turn the flash on.
         * The LED is high-intensity; the sensor AE circuit will immediately
         * start closing the aperture/reducing gain.  We must give it time.
         */
        camera_hal_set_flash(true);

        /*
         * Step 2: Discard one stale frame that was captured before the flash
         * lit — it will be dark or wrongly exposed.
         */
        discard_one_frame();

        /*
         * Step 3: Wait for AE/AWB to converge under flash illumination.
         * CONFIG_APP_CAMERA_FLASH_WARMUP_MS is set in Kconfig (default 250 ms).
         * The OV2640 runs at up to 30 fps (33 ms/frame); 250 ms ≈ 7 frames,
         * which is sufficient for AE convergence in most indoor conditions.
         */
        vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_CAMERA_FLASH_WARMUP_MS));

        ESP_LOGD(TAG, "Flash on, AE warmup %d ms complete",
                 CONFIG_APP_CAMERA_FLASH_WARMUP_MS);
    }

    sensor_t *s = camera_hal_get_sensor();
    if (!s) {
        ESP_LOGE(TAG, "Sensor not initialized!");
    } else {
        ESP_LOGI(TAG, "Sensor model: 0x%04x, status: %d",
             s->id.pid, s->status);
    }

    /* Step 4: Capture the well-exposed frame. */
    camera_fb_t *fb = esp_camera_fb_get();

    if (using_flash) {
        /*
         * Step 5: Turn flash off immediately after the frame is latched.
         * Do this before any error check so the LED is never left on.
         */
        camera_hal_set_flash(false);
    }

    if (!fb) {
        ESP_LOGE(TAG, "Frame buffer capture failed");
        return ESP_FAIL;
    }

    out_frame->fb           = fb;
    out_frame->timestamp_us = esp_timer_get_time();
    out_frame->sequence     = ++s_frame_count;

    ESP_LOGI(TAG, "Frame #%"PRIu32" captured: %zu bytes (flash=%s)",
             out_frame->sequence, fb->len,
             using_flash ? "yes" : "no");
    return ESP_OK;
}

void image_capture_return_frame(capture_frame_t *frame)
{
    if (!frame || !frame->fb) return;
    esp_camera_fb_return(frame->fb);
    frame->fb = NULL;
}

uint32_t image_capture_total_count(void)
{
    return s_frame_count;
}