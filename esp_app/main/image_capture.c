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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "image_capture";
static uint32_t s_frame_count = 0;

/**
 * @brief Safely clears the camera buffer queue.
 * This prevents the "timeout" by ensuring the DMA engine is ready.
 */
static void clear_stale_buffers(void) {
    camera_fb_t *fb = NULL;
    // Get and immediately return a frame to "flush" the sensor state
    fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

esp_err_t image_capture_acquire_frame(capture_frame_t *out_frame) {
    if (!out_frame) return ESP_ERR_INVALID_ARG;
    if (!camera_hal_is_ready()) return ESP_ERR_INVALID_STATE;

    bool using_flash = camera_hal_has_flash();

    if (using_flash) {
        camera_hal_set_flash(true);
        
        // 1. Clear the stale frame captured BEFORE the flash was on
        clear_stale_buffers();

        // 2. Wait for Auto Exposure to settle
        vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_CAMERA_FLASH_WARMUP_MS));
    }

    // 3. Capture the actual image
    // Note: We avoid heavy Log statements or I2C sensor reads here to keep timing tight
    camera_fb_t *fb = esp_camera_fb_get();

    // 4. Turn off flash immediately to save power and prevent heat
    if (using_flash) {
        camera_hal_set_flash(false);
    }

    if (!fb) {
        ESP_LOGE(TAG, "Frame buffer capture failed: Timeout or No Buffer");
        return ESP_FAIL; 
    }

    // Populate output structure
    out_frame->fb = fb;
    out_frame->timestamp_us = esp_timer_get_time();
    out_frame->sequence = ++s_frame_count;

    ESP_LOGI(TAG, "Captured Frame #%"PRIu32" [%zu bytes]", s_frame_count, fb->len);
    
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
