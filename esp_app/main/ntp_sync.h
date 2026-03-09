#pragma once

/**
 * @file ntp_sync.h
 * @brief NTP wall-clock synchronisation wrapper.
 *
 * Single Responsibility: obtain accurate wall-clock time from an NTP server
 * and make it available to the rest of the application via time() /
 * localtime() / mktime().
 *
 * Naming note
 * ───────────
 * This module is deliberately named "ntp_sync" (not "sntp_sync") to avoid a
 * symbol collision with the ESP-IDF SNTP subsystem.  esp_sntp.h (pulled in
 * transitively by lwip) already occupies the sntp_sync_* namespace:
 *   sntp_sync_mode_t   (enum)
 *   sntp_sync_time()   (function)
 * Using the same prefix causes the compiler to confuse our types with those
 * IDF symbols.  The "ntp_sync" prefix is unambiguous.
 *
 * The ESP32 has no battery-backed RTC.  After every deep-sleep wake the
 * system time resets to the Unix epoch (1970-01-01).  This module corrects
 * that by querying an NTP server so that upload_scheduler can check the
 * current date for the upload window.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ────────────────────────────────────────────────────────── */

#define NTP_SYNC_SERVER_MAX_LEN  64
#define NTP_SYNC_TZ_MAX_LEN      48

/**
 * @brief NTP synchronisation configuration.
 *
 * @note  tz_posix is a POSIX timezone string, e.g.
 *          "UTC"
 *          "EST5EDT,M3.2.0,M11.1.0"        (US Eastern)
 *          "CET-1CEST,M3.5.0,M10.5.0/3"   (Central Europe)
 *        Passed to setenv("TZ", …) / tzset() so that localtime() and
 *        mktime() return the correct local time.
 */
typedef struct {
    char     server[NTP_SYNC_SERVER_MAX_LEN];  /* e.g. "pool.ntp.org"    */
    char     tz_posix[NTP_SYNC_TZ_MAX_LEN];   /* POSIX TZ string         */
    uint32_t sync_timeout_ms;                  /* max wait for first sync */
} ntp_sync_config_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the SNTP client and apply the timezone.
 *
 * Must be called after WiFi is connected.  Returns immediately; sync happens
 * asynchronously in the lwIP SNTP task.  Use ntp_sync_wait() to block until
 * a valid timestamp is available.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if cfg is NULL or has an
 *         empty server string.
 */
esp_err_t ntp_sync_init(const ntp_sync_config_t *cfg);

/**
 * @brief Block until synchronisation completes or @p timeout_ms elapses.
 *
 * @param timeout_ms  Maximum milliseconds to wait.
 * @return ESP_OK if the clock was set, ESP_ERR_TIMEOUT otherwise.
 */
esp_err_t ntp_sync_wait(uint32_t timeout_ms);

/**
 * @brief True once at least one successful sync has been received.
 */
bool ntp_sync_is_done(void);

/**
 * @brief Stop the SNTP client.  Call before WiFi disconnect or deep sleep.
 */
void ntp_sync_deinit(void);

#ifdef __cplusplus
}
#endif