#pragma once

/**
 * @file image_capture.h
 * @brief Image capture module.
 *
 * Single Responsibility: acquire frames from the camera driver and expose
 * them via a simple borrow/return API.  Frame-buffer memory is owned by
 * the driver; callers must always call image_capture_return_frame() after
 * consuming a frame.
 *
 * The module is decoupled from the HTTP server and WiFi layers — it knows
 * nothing about how frames are transmitted (Dependency Inversion for C).
 */

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Frame descriptor ─────────────────────────────────────────────────────── */

/**
 * @brief Lightweight wrapper around camera_fb_t.
 *
 * Using an intermediate type lets us add metadata (timestamp, sequence
 * counter, …) without changing every consumer — analogous to the
 * Open/Closed principle.
 */
typedef struct {
    camera_fb_t  *fb;          /* raw driver frame buffer (do not free)  */
    int64_t       timestamp_us; /* capture time in microseconds           */
    uint32_t      sequence;    /* monotonically increasing capture count  */
} capture_frame_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Acquire a single frame from the camera.
 *
 * Caller receives ownership of the frame descriptor and MUST call
 * image_capture_return_frame() when done.
 *
 * @param[out] out_frame  Populated on success.
 * @return ESP_OK, ESP_ERR_INVALID_STATE (camera not ready),
 *         ESP_FAIL (driver error).
 */
esp_err_t image_capture_acquire_frame(capture_frame_t *out_frame);

/**
 * @brief Return a previously acquired frame to the driver pool.
 *
 * Must be called exactly once per successful image_capture_acquire_frame().
 */
void image_capture_return_frame(capture_frame_t *frame);

/**
 * @brief Total number of frames successfully captured since boot.
 */
uint32_t image_capture_total_count(void);

#ifdef __cplusplus
}
#endif