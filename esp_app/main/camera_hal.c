/**
 * @file camera_hal.c
 * @brief Camera Hardware Abstraction Layer – implementation.
 *
 * Only this translation unit knows about esp_camera internals.
 * All other modules use the opaque API from camera_hal.h.
 */

#include "camera_hal.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "camera_hal";

/* ── Internal state ───────────────────────────────────────────────────────── */

static bool s_camera_ready  = false;
static int  s_flash_pin     = -1;   /* -1 = no flash configured */

/* ── Board pin tables ─────────────────────────────────────────────────────── */

/*
 * One struct per board keeps additions O(1) — add a new row in the table
 * without touching any logic (Open/Closed adapted for C via a data table).
 */
typedef struct {
    camera_board_t board;
    int pwdn, reset, xclk, sda, scl;
    int d7, d6, d5, d4, d3, d2, d1, d0;
    int vsync, href, pclk;
    int xclk_freq_hz;
    int flash_led;             /* -1 = no flash on this board */
    ledc_timer_t   ledc_timer;
    ledc_channel_t ledc_channel;
} board_pin_map_t;

static const board_pin_map_t BOARD_PIN_MAPS[] = {
    {
        .board        = CAMERA_BOARD_AI_THINKER,
        .pwdn = 32,  .reset = -1, .xclk = 0,
        .sda  = 26,  .scl   = 27,
        .d7   = 35,  .d6 = 34, .d5 = 39, .d4 = 36,
        .d3   = 21,  .d2 = 19, .d1 = 18, .d0 =  5,
        .vsync = 25, .href = 23, .pclk = 22,
        .xclk_freq_hz = 20000000,
        .flash_led    = 4,     /* white LED on GPIO 4, active HIGH */
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
    },
    {
        .board        = CAMERA_BOARD_WROVER_KIT,
        .pwdn = -1,  .reset = -1, .xclk = 21,
        .sda  = 26,  .scl   = 27,
        .d7   = 35,  .d6 = 34, .d5 = 39, .d4 = 36,
        .d3   = 19,  .d2 = 18, .d1 =  5, .d0 =  4,
        .vsync = 25, .href = 23, .pclk = 22,
        .xclk_freq_hz = 20000000,
        .flash_led    = -1,    /* no flash LED on WROVER KIT */
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
    },
    {
        .board        = CAMERA_BOARD_ESP_EYE,
        .pwdn = -1,  .reset = -1, .xclk = 4,
        .sda  = 18,  .scl   = 23,
        .d7   = 36,  .d6 = 37, .d5 = 38, .d4 = 39,
        .d3   = 35,  .d2 = 14, .d1 = 13, .d0 = 34,
        .vsync = 5,  .href = 27, .pclk = 25,
        .xclk_freq_hz = 20000000,
        .flash_led    = -1,    /* no flash LED on ESP-EYE */
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
    },
    {
        .board        = CAMERA_BOARD_M5STACK_PSRAM,
        .pwdn = -1,  .reset = 15, .xclk = 27,
        .sda  = 25,  .scl   = 23,
        .d7   = 19,  .d6 = 36, .d5 = 18, .d4 = 39,
        .d3   = 5,   .d2 = 34, .d1 = 35, .d0 = 17,
        .vsync = 22, .href = 26, .pclk = 21,
        .xclk_freq_hz = 20000000,
        .flash_led    = -1,    /* no flash LED on M5Stack PSRAM */
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
    },
};

static const size_t BOARD_PIN_MAP_COUNT =
    sizeof(BOARD_PIN_MAPS) / sizeof(BOARD_PIN_MAPS[0]);

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static const board_pin_map_t *find_board_pins(camera_board_t board)
{
    for (size_t i = 0; i < BOARD_PIN_MAP_COUNT; i++) {
        if (BOARD_PIN_MAPS[i].board == board) {
            return &BOARD_PIN_MAPS[i];
        }
    }
    return NULL;
}

static camera_config_t build_driver_config(const camera_hal_config_t *cfg)
{
    camera_config_t drv = {
        .pin_pwdn     = cfg->pin_pwdn,
        .pin_reset    = cfg->pin_reset,
        .pin_xclk     = cfg->pin_xclk,
        .pin_sccb_sda = cfg->pin_sda,
        .pin_sccb_scl = cfg->pin_scl,
        .pin_d7       = cfg->pin_d7,
        .pin_d6       = cfg->pin_d6,
        .pin_d5       = cfg->pin_d5,
        .pin_d4       = cfg->pin_d4,
        .pin_d3       = cfg->pin_d3,
        .pin_d2       = cfg->pin_d2,
        .pin_d1       = cfg->pin_d1,
        .pin_d0       = cfg->pin_d0,
        .pin_vsync    = cfg->pin_vsync,
        .pin_href     = cfg->pin_href,
        .pin_pclk     = cfg->pin_pclk,
        .xclk_freq_hz = cfg->xclk_freq_hz,
        .ledc_timer   = cfg->ledc_timer,
        .ledc_channel = cfg->ledc_channel,
        .pixel_format = cfg->pixel_format,
        .frame_size   = cfg->frame_size,
        .jpeg_quality = cfg->jpeg_quality,
        .fb_count     = cfg->fb_count,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };
    return drv;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t camera_hal_config_for_board(camera_hal_config_t *cfg,
                                      camera_board_t       board)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (board == CAMERA_BOARD_CUSTOM) return ESP_OK; /* caller fills pins */

    const board_pin_map_t *pm = find_board_pins(board);
    if (!pm) {
        ESP_LOGE(TAG, "Unknown board preset %d", (int)board);
        return ESP_ERR_NOT_FOUND;
    }

    cfg->board         = board;
    cfg->pin_pwdn      = pm->pwdn;
    cfg->pin_reset     = pm->reset;
    cfg->pin_xclk      = pm->xclk;
    cfg->pin_sda       = pm->sda;
    cfg->pin_scl       = pm->scl;
    cfg->pin_d7        = pm->d7;
    cfg->pin_d6        = pm->d6;
    cfg->pin_d5        = pm->d5;
    cfg->pin_d4        = pm->d4;
    cfg->pin_d3        = pm->d3;
    cfg->pin_d2        = pm->d2;
    cfg->pin_d1        = pm->d1;
    cfg->pin_d0        = pm->d0;
    cfg->pin_vsync     = pm->vsync;
    cfg->pin_href      = pm->href;
    cfg->pin_pclk      = pm->pclk;
    cfg->xclk_freq_hz  = pm->xclk_freq_hz;
    cfg->ledc_timer    = pm->ledc_timer;
    cfg->ledc_channel  = pm->ledc_channel;
    cfg->pin_flash_led = pm->flash_led;
    return ESP_OK;
}

esp_err_t camera_hal_init(const camera_hal_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_camera_ready) {
        ESP_LOGW(TAG, "Camera already initialised");
        return ESP_OK;
    }

    camera_config_t drv_cfg = build_driver_config(cfg);
    esp_err_t err = esp_camera_init(&drv_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure flash LED GPIO if one is defined for this board. */
    s_flash_pin = cfg->pin_flash_led;
    if (s_flash_pin >= 0) {
        gpio_config_t flash_io = {
            .pin_bit_mask = (1ULL << (uint32_t)s_flash_pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&flash_io));
        /* Ensure flash is OFF at startup — never leave it on after a crash. */
        gpio_set_level((gpio_num_t)s_flash_pin, 0);
        ESP_LOGI(TAG, "Flash LED configured on GPIO %d", s_flash_pin);
    }

    s_camera_ready = true;
    ESP_LOGI(TAG, "Camera initialised (board=%d, fmt=%d, size=%d, flash=%d)",
             (int)cfg->board, (int)cfg->pixel_format,
             (int)cfg->frame_size, s_flash_pin);
    return ESP_OK;
}

esp_err_t camera_hal_deinit(void)
{
    if (!s_camera_ready) return ESP_OK;

    /* Guarantee flash is off before the GPIO is released. */
    if (s_flash_pin >= 0) {
        gpio_set_level((gpio_num_t)s_flash_pin, 0);
    }

    esp_err_t err = esp_camera_deinit();
    if (err == ESP_OK) {
        s_camera_ready = false;
        s_flash_pin    = -1;
    }
    return err;
}

bool camera_hal_is_ready(void) { return s_camera_ready; }

sensor_t *camera_hal_get_sensor(void)
{
    if (!s_camera_ready) return NULL;
    return esp_camera_sensor_get();
}

esp_err_t camera_hal_set_flash(bool on)
{
    if (s_flash_pin < 0) return ESP_ERR_NOT_SUPPORTED;
    gpio_set_level((gpio_num_t)s_flash_pin, on ? 1 : 0);
    return ESP_OK;
}

bool camera_hal_has_flash(void)
{
    return (s_flash_pin >= 0);
}