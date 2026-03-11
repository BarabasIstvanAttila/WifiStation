/**
 * @file image_capture.h
 * @brief Thread-safe image capture API with continuous flash fill.
 */

#ifndef IMAGE_CAPTURE_H
#define IMAGE_CAPTURE_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure to hold the captured frame and its metadata.
 */
typedef struct {
    camera_fb_t *fb;       /*!< Pointer to the camera frame buffer */
    int64_t timestamp_us;  /*!< Timestamp of capture in microseconds */
    uint32_t sequence;     /*!< Sequence number of the frame */
} capture_frame_t;

/**
 * @brief Initializes the image capture module and its mutex.
 * @return ESP_OK on success, or an error code if the mutex fails to create.
 */
esp_err_t image_capture_init(void);

/**
 * @brief Tunes the camera sensor for better low-light exposure.
 * @return ESP_OK on success.
 */
esp_err_t image_capture_tune_exposure(void);

/**
 * @brief Captures a single frame safely, managing flash and exposure delays.
 * @param out_frame Pointer to the structure where frame data will be stored.
 * @return ESP_OK on success, or an error code if capture times out.
 */
esp_err_t image_capture_acquire_frame(capture_frame_t *out_frame);

/**
 * @brief Returns the frame buffer memory back to the camera driver.
 * @param frame Pointer to the frame structure to be freed.
 */
void image_capture_return_frame(capture_frame_t *frame);

/**
 * @brief Gets the total number of frames captured since boot.
 * @return Total frame count.
 */
uint32_t image_capture_total_count(void);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_CAPTURE_H */
