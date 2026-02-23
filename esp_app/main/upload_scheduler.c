/**
 * @file upload_scheduler.c
 * @brief Upload scheduler — window-based daily trigger with deep sleep.
 */

#include "upload_scheduler.h"

#include <string.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_attr.h"     /* RTC_DATA_ATTR */

static const char *TAG = "upload_scheduler";

/* ── RTC slow memory — survives deep sleep ────────────────────────────────── */

/*
 * Three variables in the 8 KB RTC slow memory region.
 * Initialised to 0 on first power-on; preserved across every deep sleep.
 *
 * day:   1–31  (tm_mday, stored as-is)
 * month: 1–12  (tm_mon + 1 to avoid confusion with the zero sentinel)
 * year:  full year, e.g. 2025
 *
 * Together they form the date of the most recent successful upload.
 * A zero year means no upload has ever been recorded.
 */
static RTC_DATA_ATTR uint8_t  s_rtc_last_day   = 0;
static RTC_DATA_ATTR uint8_t  s_rtc_last_month  = 0;
static RTC_DATA_ATTR uint16_t s_rtc_last_year   = 0;

/* ── Days-per-month table ─────────────────────────────────────────────────── */

static const int DAYS_PER_MONTH[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* ── Public API — date helpers ────────────────────────────────────────────── */

int upload_scheduler_days_in_month(int month, int year)
{
    /* month is 0-based (tm_mon): 0=Jan … 11=Dec */
    if (month < 0 || month > 11) return 30;

    if (month == 1) { /* February */
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) ||
                    (year % 400 == 0);
        return leap ? 29 : 28;
    }

    return DAYS_PER_MONTH[month];
}

bool upload_scheduler_is_within_upload_window(uint8_t window_days)
{
    time_t    now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    int year     = 1900 + t.tm_year;
    int month_0  = t.tm_mon;          /* 0-based */
    int today    = t.tm_mday;
    int last_day = upload_scheduler_days_in_month(month_0, year);

    /*
     * Days remaining including today: last_day - today + 1
     * Window condition: days remaining ≤ window_days
     *
     * Equivalently: today >= last_day - window_days + 1
     *
     * Example: Jan, last_day=31, window_days=5
     *   today=27 → 31-27+1 = 5 ≤ 5 → IN window
     *   today=26 → 31-26+1 = 6 > 5 → NOT in window
     */
    int days_remaining = last_day - today + 1;
    bool in_window     = (days_remaining <= (int)window_days);

    ESP_LOGI(TAG, "Date: %04d-%02d-%02d  last_day=%d  days_remaining=%d"
                  "  window=%d  in_window=%s",
             year, month_0 + 1, today,
             last_day, days_remaining, window_days,
             in_window ? "YES" : "no");

    return in_window;
}

/* ── Public API — RTC state ───────────────────────────────────────────────── */

bool upload_scheduler_already_uploaded_today(void)
{
    time_t    now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    uint8_t  cur_day   = (uint8_t)t.tm_mday;
    uint8_t  cur_month = (uint8_t)(t.tm_mon + 1);
    uint16_t cur_year  = (uint16_t)(1900 + t.tm_year);

    /*
     * All three fields must match: a new calendar day always triggers a new
     * upload, even if the month and year are the same.
     */
    bool done = (s_rtc_last_day   == cur_day)  &&
                (s_rtc_last_month == cur_month) &&
                (s_rtc_last_year  == cur_year);

    ESP_LOGI(TAG, "RTC: last_upload=%04d-%02d-%02d  today=%04d-%02d-%02d  %s",
             s_rtc_last_year,  s_rtc_last_month,  s_rtc_last_day,
             cur_year, cur_month, cur_day,
             done ? "ALREADY DONE TODAY" : "not yet uploaded today");

    return done;
}

void upload_scheduler_mark_uploaded(void)
{
    time_t    now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    s_rtc_last_day   = (uint8_t)t.tm_mday;
    s_rtc_last_month = (uint8_t)(t.tm_mon + 1);
    s_rtc_last_year  = (uint16_t)(1900 + t.tm_year);

    ESP_LOGI(TAG, "Marked upload done for %04d-%02d-%02d",
             s_rtc_last_year, s_rtc_last_month, s_rtc_last_day);
}

/* ── Public API — sleep control ───────────────────────────────────────────── */

uint64_t upload_scheduler_seconds_until_next_check(
    const upload_scheduler_config_t *cfg)
{
    if (!cfg) return 86400;

    time_t    now = time(NULL);
    struct tm next_wake;
    localtime_r(&now, &next_wake);

    /*
     * Target: tomorrow at check_hour:check_minute:00.
     * Adding 1 to tm_mday and calling mktime() is safe — mktime() normalises
     * the struct, so Dec 31 + 1 day correctly becomes Jan 1 of the next year,
     * and DST transitions are accounted for via tm_isdst = -1.
     */
    next_wake.tm_mday += 1;
    next_wake.tm_hour  = (int)cfg->check_hour;
    next_wake.tm_min   = (int)cfg->check_minute;
    next_wake.tm_sec   = 0;
    next_wake.tm_isdst = -1;

    time_t next      = mktime(&next_wake);
    double diff_secs = difftime(next, now);

    if (diff_secs <= 0) {
        diff_secs += 86400.0; /* guard: should never be needed */
    }

    ESP_LOGI(TAG, "Next check in %.0f s (%02d:%02d tomorrow)",
             diff_secs, cfg->check_hour, cfg->check_minute);

    return (uint64_t)diff_secs;
}

void upload_scheduler_deep_sleep(uint64_t seconds)
{
    uint64_t us = seconds * 1000000ULL;

    ESP_LOGI(TAG, "Deep sleep for %llu s (~%.1f h)",
             seconds, (double)seconds / 3600.0);

    esp_sleep_enable_timer_wakeup(us);
    esp_deep_sleep_start(); /* does not return */
}