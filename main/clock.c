#include "clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "civil.h"
#include "event_log.h"
#include "rtc_ds3231.h"

#define SYNC_THRESHOLD_S 30

static const char *TAG = "clock";

static SemaphoreHandle_t s_lock;
static bool s_rtc_ok;
static bool s_valid;
static bool s_sntp_started;
static clock_set_cb_t s_cb;

// Firmware build date (UTC midnight). Any RTC time before this is invalid.
static time_t build_time(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = "";
    int day = 1, year = 2025;
    sscanf(esp_app_get_description()->date, "%3s %d %d", mon, &day, &year);
    const char *p = strstr(months, mon);
    int m = p ? (int)(p - months) / 3 + 1 : 1;
    return (time_t)(days_from_civil(year, m, day) * 86400);
}

void clock_init(void)
{
    setenv("TZ", CONFIG_WATER_TZ, 1);
    tzset();
    s_lock = xSemaphoreCreateMutex();

    s_rtc_ok = ds3231_init(CONFIG_WATER_RTC_SDA_GPIO, CONFIG_WATER_RTC_SCL_GPIO) == ESP_OK;
    if (!s_rtc_ok) {
        ESP_LOGE(TAG, "RTC not responding");
        return;
    }

    bool osf = true;
    time_t t = 0;
    if (ds3231_get_osf(&osf) != ESP_OK || osf) {
        ESP_LOGW(TAG, "RTC oscillator stopped; time lost");
        return;
    }
    if (ds3231_get_time(&t) != ESP_OK || t < build_time()) {
        ESP_LOGW(TAG, "RTC time invalid");
        return;
    }
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);
    s_valid = true;
    ESP_LOGI(TAG, "time from RTC: %lld", (long long)t);
}

void clock_set_callback(clock_set_cb_t cb)
{
    s_cb = cb;
}

bool clock_is_valid(void)
{
    return s_valid;
}

bool clock_rtc_ok(void)
{
    return s_rtc_ok;
}

time_t clock_now(void)
{
    return time(NULL);
}

// Write t to the RTC (system time already set). Caller holds s_lock.
static void store_in_rtc(time_t t)
{
    if (s_rtc_ok && (ds3231_set_time(t) != ESP_OK || ds3231_clear_osf() != ESP_OK)) {
        ESP_LOGE(TAG, "RTC write failed");
    }
}

static void notify(bool was_valid, int64_t delta)
{
    if (delta > INT32_MAX) {
        delta = INT32_MAX;
    } else if (delta < INT32_MIN) {
        delta = INT32_MIN;
    }
    event_log_add(EV_TIME_SET, 0, (int32_t)delta);
    if (s_cb) {
        s_cb(was_valid);
    }
}

void clock_set(time_t t)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool was_valid = s_valid;
    int64_t delta = (int64_t)t - (int64_t)time(NULL);
    if (was_valid && llabs(delta) <= SYNC_THRESHOLD_S) {
        xSemaphoreGive(s_lock);
        return;
    }
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);
    store_in_rtc(t);
    s_valid = true;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "time set from phone (delta %lld s)", (long long)delta);
    notify(was_valid, delta);
}

// SNTP has already set the system time when this runs.
static void on_sntp_sync(struct timeval *tv)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool was_valid = s_valid;
    int64_t delta = 0;
    time_t rtc = 0;
    if (s_rtc_ok && ds3231_get_time(&rtc) == ESP_OK) {
        delta = (int64_t)tv->tv_sec - (int64_t)rtc;
    }
    bool changed = !was_valid || llabs(delta) > SYNC_THRESHOLD_S;
    if (changed) {
        store_in_rtc(tv->tv_sec);
    }
    s_valid = true;
    xSemaphoreGive(s_lock);

    if (changed) {
        ESP_LOGI(TAG, "time set from SNTP (delta %lld s)", (long long)delta);
        notify(was_valid, delta);
    }
}

void clock_start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }
    s_sntp_started = true;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
}
