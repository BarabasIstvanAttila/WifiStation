#pragma once

/**
 * @file camera_hal.h
 * @brief Camera Hardware Abstraction Layer
 *
 * Single Responsibility: owns all camera hardware pin configuration,
 * sensor settings, and lifecycle (init / deinit).
 *
 * Consumers depend on this interface, not on esp_camera directly,
 * so the underlying driver can be swapped without touching callers
 * (Dependency Inversion adapted for C via opaque handles + function pointers).
 */

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Board presets ────────────────────────────────────────────────────────── */

typedef enum {
    CAMERA_BOARD_AI_THINKER,   /* OV2640, most common */
    CAMERA_BOARD_WROVER_KIT,
    CAMERA_BOARD_ESP_EYE,
    CAMERA_BOARD_M5STACK_PSRAM,
    CAMERA_BOARD_CUSTOM,       /* caller fills camera_hal_config_t manually  */
} camera_board_t;

/* ── Configuration ────────────────────────────────────────────────────────── */

/**
 * @brief Full camera configuration.
 *
 * When board != CAMERA_BOARD_CUSTOM the pin fields are filled automatically
 * by camera_hal_config_for_board(); callers only need to override pixel_format
 * and frame_size.
 */
typedef struct {
    camera_board_t    board;
    pixformat_t       pixel_format;   /* PIXFORMAT_JPEG, PIXFORMAT_RGB565 … */
    framesize_t       frame_size;     /* FRAMESIZE_QVGA, FRAMESIZE_VGA …   */
    int               jpeg_quality;   /* 0–63, lower = better quality       */
    size_t            fb_count;       /* frame-buffer count (≥2 for stream) */

    /* Raw pin map — populated by camera_hal_config_for_board() or by caller */
    int pin_pwdn;
    int pin_reset;
    int pin_xclk;
    int pin_sda;
    int pin_scl;
    int pin_d7, pin_d6, pin_d5, pin_d4;
    int pin_d3, pin_d2, pin_d1, pin_d0;
    int pin_vsync;
    int pin_href;
    int pin_pclk;
    int xclk_freq_hz;
    ledc_timer_t   ledc_timer;
    ledc_channel_t ledc_channel;

    /**
     * GPIO pin connected to the on-board flash LED.
     * Set to -1 when the board has no flash LED or flash is not used.
     * On AI Thinker this is GPIO 4 (high-intensity white LED, active HIGH).
     * The HAL configures this pin as push-pull output during camera_hal_init()
     * and ensures it is driven LOW (off) at startup and after deinit.
     */
    int pin_flash_led;
} camera_hal_config_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Populate pin fields in @p cfg for a known board preset.
 *        Call before camera_hal_init() when not using CAMERA_BOARD_CUSTOM.
 */
esp_err_t camera_hal_config_for_board(camera_hal_config_t *cfg,
                                      camera_board_t       board);

/**
 * @brief Initialise the camera peripheral.
 * @return ESP_OK on success, ESP_ERR_* otherwise.
 */
esp_err_t camera_hal_init(const camera_hal_config_t *cfg);

/**
 * @brief Release camera resources.
 */
esp_err_t camera_hal_deinit(void);

/**
 * @brief Return true when the camera has been successfully initialised.
 */
bool camera_hal_is_ready(void);

/**
 * @brief Apply run-time sensor settings (brightness, saturation, …).
 *        Sensor pointer is owned by the driver; do not free it.
 */
sensor_t *camera_hal_get_sensor(void);

/**
 * @brief Control the on-board flash LED.
 *
 * Drives the flash GPIO HIGH (on) or LOW (off).
 * Safe to call at any time after camera_hal_init().
 *
 * @return ESP_OK if the flash was controlled, ESP_ERR_NOT_SUPPORTED
 *         when no flash pin was configured (pin_flash_led == -1).
 */
esp_err_t camera_hal_set_flash(bool on);

/**
 * @brief True if a flash LED pin was configured during init.
 */
bool camera_hal_has_flash(void);

#ifdef __cplusplus
}
#endif