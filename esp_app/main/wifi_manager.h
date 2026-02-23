#pragma once

/**
 * @file wifi_manager.h
 * @brief WiFi connectivity management.
 *
 * Single Responsibility: own the station-mode WiFi lifecycle.
 * Nothing in this module knows about cameras or HTTP.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ────────────────────────────────────────────────────────── */

#define WIFI_MANAGER_MAX_SSID_LEN     32
#define WIFI_MANAGER_MAX_PASSWORD_LEN 64
#define WIFI_MANAGER_DEFAULT_MAX_RETRY 10

typedef struct {
    char     ssid[WIFI_MANAGER_MAX_SSID_LEN];
    char     password[WIFI_MANAGER_MAX_PASSWORD_LEN];
    uint8_t  max_retry;          /* 0 = use WIFI_MANAGER_DEFAULT_MAX_RETRY */
} wifi_manager_config_t;

/* ── Callback ─────────────────────────────────────────────────────────────── */

/**
 * @brief Connection-state callback type.
 *
 * Using a function pointer keeps wifi_manager independent of any specific
 * consumer (Dependency Inversion for C) — register whoever cares.
 *
 * @param connected  true = got IP, false = disconnected / failed.
 * @param ip_str     Dotted-decimal IP string when connected, NULL otherwise.
 * @param ctx        Opaque user context passed at registration.
 */
typedef void (*wifi_manager_state_cb_t)(bool        connected,
                                        const char *ip_str,
                                        void       *ctx);

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Initialise and connect to the configured AP.
 *
 * Blocks until an IP address is obtained or max_retry is exhausted.
 *
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if max retries exceeded.
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t *cfg);

/**
 * @brief Disconnect and release WiFi resources.
 */
void wifi_manager_deinit(void);

/**
 * @brief Register a callback for connection state changes.
 *        Only one callback is supported; pass NULL to unregister.
 */
void wifi_manager_register_state_cb(wifi_manager_state_cb_t cb, void *ctx);

/**
 * @brief True if an IP address is currently held.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Copy the current IP address string into @p buf (size ≥ 16).
 * @return ESP_OK or ESP_ERR_INVALID_STATE if not connected.
 */
esp_err_t wifi_manager_get_ip(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif