#pragma once

/**
 * @file upload_scheduler.h
 * @brief Upload scheduler — window-based daily trigger with deep sleep.
 *
 * Single Responsibility: determine WHEN to upload, track WHETHER today's
 * upload has already happened, and manage deep sleep between wake cycles.
 *
 * Upload window
 * ─────────────
 * The device uploads once per day during the last N days of each month
 * (N is configurable via Kconfig, default 5).  Example with N=5 in January:
 *   Upload on: Jan 27, 28, 29, 30, 31
 *   Skip on:   Jan 1 – 26
 *
 * Per-day deduplication
 * ─────────────────────
 * RTC slow memory tracks the last upload date (year + month + day).
 * If the device reboots mid-day the flag prevents a double upload.
 * The flag is NOT carried across days — each day in the window gets its
 * own upload, regardless of what happened the previous day.
 *
 * Deep sleep model
 * ────────────────
 * Deep sleep powers off all CPU cores and peripherals (~10 µA).
 * Only the RTC subsystem stays alive.  On timer wake the chip performs a
 * full boot — app_main() runs from the beginning every day.
 *
 * RTC_DATA_ATTR variables survive every deep sleep / wake cycle without
 * NVS writes.  They are cleared only on a full power-off.
 *
 * Wake cycle
 * ──────────
 * 1. Boot → init → WiFi → SNTP sync → check date.
 * 2. If within upload window AND not yet uploaded today:
 *      flash on → capture → flash off → upload → mark done.
 * 3. Teardown (WiFi, camera).
 * 4. Calculate seconds until tomorrow at check_hour:check_minute.
 * 5. Enter deep sleep — does not return.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t check_hour;      /* 0–23: local hour the daily check runs        */
    uint8_t check_minute;    /* 0–59: minute within that hour                */
    uint8_t upload_window_days; /* upload during last N days of month        */
} upload_scheduler_config_t;

/* ── Date helpers ─────────────────────────────────────────────────────────── */

/**
 * @brief Return the number of days in @p month of @p year.
 * @param month  0-based (tm_mon): 0 = January … 11 = December.
 * @param year   Full year, e.g. 2025.
 */
int upload_scheduler_days_in_month(int month, int year);

/**
 * @brief True if today falls within the last @p window_days of the month.
 *
 * Examples with window_days = 5 in a 31-day month:
 *   mday 27 → true  (31 - 27 + 1 = 5)
 *   mday 26 → false (31 - 26 + 1 = 6 > 5)
 *   mday 31 → true
 *
 * Requires that the system clock has been set by SNTP first.
 */
bool upload_scheduler_is_within_upload_window(uint8_t window_days);

/* ── RTC state — survives deep sleep ─────────────────────────────────────── */

/**
 * @brief True if today's upload has already been recorded in RTC memory.
 *
 * Compares the stored year + month + day against today's local date.
 * Returns false on a different calendar day even within the same month,
 * so each day in the window triggers its own upload.
 */
bool upload_scheduler_already_uploaded_today(void);

/**
 * @brief Record that today's upload is complete.
 *        Writes today's year, month, day into RTC slow memory.
 */
void upload_scheduler_mark_uploaded(void);

/* ── Sleep control ────────────────────────────────────────────────────────── */

/**
 * @brief Calculate seconds from now until tomorrow at check_hour:check_minute.
 *
 * Always returns a positive value.  mktime() handles month and year rollovers
 * and DST transitions automatically.
 *
 * Requires valid system time (call after sntp_sync_wait() succeeds).
 */
uint64_t upload_scheduler_seconds_until_next_check(
    const upload_scheduler_config_t *cfg);

/**
 * @brief Enter deep sleep for @p seconds.
 *
 * This function does NOT return.  The device wakes, boots, and re-enters
 * app_main() after the specified duration.
 */
void upload_scheduler_deep_sleep(uint64_t seconds);

#ifdef __cplusplus
}
#endif