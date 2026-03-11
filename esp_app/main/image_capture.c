/**
 * @file image_capture.c
 * @brief Thread-safe image capture implementation with continuous flash fill.
 */

#include "image_capture.h"
#include "camera_hal.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "image_capture";

/* ── Module state ─────────────────────────────────────────────────────────── */

static uint32_t s_frame_count = 0;
static SemaphoreHandle_t s_capture_mutex = NULL;

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/**
 * @brief Safely clears the camera buffer queue to prevent timeouts.
 */
static void clear_stale_buffers(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t image_capture_init(void) 
{
    if (s_capture_mutex == NULL) {
        s_capture_mutex = xSemaphoreCreateMutex();
        if (s_capture_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create capture mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t image_capture_tune_exposure(void) {
    sensor_t *s = camera_hal_get_sensor();
    if (!s) {
        ESP_LOGE(TAG, "Cannot tune exposure: Sensor not found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tuning sensor exposure settings...");
    s->set_ae_level(s, 2); 
    s->set_brightness(s, 1);
    s->set_gainceiling(s, GAINCEILING_64X); 
    s->set_exposure_ctrl(s, 1); 
    s->set_gain_ctrl(s, 1);     

    return ESP_OK;
}

esp_err_t image_capture_acquire_frame(capture_frame_t *out_frame)
{
    if (!out_frame) return ESP_ERR_INVALID_ARG;

    if (!camera_hal_is_ready()) {
        ESP_LOGE(TAG, "Camera not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Ensure initialized and grab mutex for thread safety
    if (s_capture_mutex == NULL) {
        ESP_LOGE(TAG, "Capture module not initialized. Call image_capture_init() first.");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_capture_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire capture mutex");
        return ESP_ERR_TIMEOUT;
    }

    bool using_flash = camera_hal_has_flash();

    if (using_flash) {
        /* Turn the flash ON continuously */
        camera_hal_set_flash(true);

        /* Discard stale frame to prevent timeout */
        clear_stale_buffers();

        /* Continuous Fill Delay: 2.5 seconds to fill room and adjust AE */
        vTaskDelay(pdMS_TO_TICKS(2500)); 
        ESP_LOGD(TAG, "Room filled with light, AE warmup complete");
    }

    sensor_t *s = camera_hal_get_sensor();
    if (!s) {
        ESP_LOGE(TAG, "Sensor not initialized!");
    } else {
        ESP_LOGI(TAG, "Sensor model: 0x%04x, status: %d", s->id.pid, s->status);
    }

    /* Capture the well-exposed frame */
    camera_fb_t *fb = esp_camera_fb_get();

    /* Turn flash OFF immediately to prevent overheating */
    if (using_flash) {
        camera_hal_set_flash(false);
    }

    if (!fb) {
        ESP_LOGE(TAG, "Frame buffer capture failed");
        xSemaphoreGive(s_capture_mutex);
        return ESP_FAIL;
    }

    out_frame->fb           = fb;
    out_frame->timestamp_us = esp_timer_get_time();
    out_frame->sequence     = ++s_frame_count;

    ESP_LOGI(TAG, "Frame #%"PRIu32" captured: %zu bytes (flash=%s)",
             out_frame->sequence, fb->len,
             using_flash ? "yes" : "no");
             
    // Release the mutex
    xSemaphoreGive(s_capture_mutex);
    
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
