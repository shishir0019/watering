#include "controller.h"

#include <string.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "clock.h"
#include "event_log.h"
#include "relay.h"
#include "scheduler.h"
#include "store.h"

#define TICK_MS     200
#define QUEUE_LEN   (MAX_PUMPS * 2)
#define US_PER_S    1000000LL

typedef struct {
    bool         running;
    run_source_t source;
    uint16_t     seconds;       // requested duration
    time_t       start;         // wall clock
    int64_t      start_us;      // monotonic
} pump_run_t;

typedef struct {
    uint8_t  pump;
    uint8_t  source;
    uint16_t seconds;
} pending_t;

static const char *TAG = "controller";

static SemaphoreHandle_t s_lock;
static pump_run_t s_run[MAX_PUMPS];
static time_t s_last_start[MAX_PUMPS];   // guards against running one schedule time twice
static pending_t s_queue[QUEUE_LEN];
static int s_qlen;

static int64_t s_boot_us;
static bool s_settled;
static bool s_catchup_pending = true;
static int64_t s_last_min = -1;          // last local minute checked for schedules (-1 = restart)
static int64_t s_check_from_min;         // first minute checked live; catch-up covers earlier times

static int pump_count(void)
{
    return store_pump_count();
}

static int running_count(void)
{
    int n = 0;
    for (int p = 0; p < pump_count(); p++) {
        n += s_run[p].running;
    }
    return n;
}

static bool is_queued(int pump)
{
    for (int i = 0; i < s_qlen; i++) {
        if (s_queue[i].pump == pump) {
            return true;
        }
    }
    return false;
}

static void enqueue(int pump, run_source_t src, uint16_t seconds)
{
    if (is_queued(pump) || s_qlen >= QUEUE_LEN) {
        return;
    }
    s_queue[s_qlen++] = (pending_t){ .pump = (uint8_t)pump, .source = (uint8_t)src, .seconds = seconds };
}

static void dequeue_pump(int pump)
{
    int j = 0;
    for (int i = 0; i < s_qlen; i++) {
        if (s_queue[i].pump != pump) {
            s_queue[j++] = s_queue[i];
        }
    }
    s_qlen = j;
}

static uint16_t clamp_seconds(int seconds)
{
    if (seconds < MIN_RUN_S) {
        return MIN_RUN_S;
    }
    if (seconds > CONFIG_WATER_MAX_RUN_S) {
        return CONFIG_WATER_MAX_RUN_S;
    }
    return (uint16_t)seconds;
}

static void start_run(int pump, run_source_t src, int seconds)
{
    pump_run_t *r = &s_run[pump];
    r->running = true;
    r->source = src;
    r->seconds = clamp_seconds(seconds);
    r->start = clock_is_valid() ? clock_now() : 0;   // 0 = time unknown
    r->start_us = esp_timer_get_time();
    s_last_start[pump] = r->start;

    // Persist before switching on so a power cut during the run is detected.
    busy_t busy = { .start = r->start, .seconds = r->seconds, .active = 1, .source = (uint8_t)src };
    store_set_busy(pump, &busy);
    relay_set(pump, true);
    event_log_add(EV_PUMP_START, pump + 1, src);
}

static void finish_run(int pump, bool stopped)
{
    pump_run_t *r = &s_run[pump];
    relay_set(pump, false);
    r->running = false;

    int64_t ran = (esp_timer_get_time() - r->start_us + US_PER_S / 2) / US_PER_S;
    if (ran > r->seconds) {
        ran = r->seconds;
    }
    last_run_t last = { .start = r->start, .seconds = (uint16_t)ran, .source = (uint8_t)r->source };
    store_set_last(pump, &last);
    busy_t idle = { 0 };
    store_set_busy(pump, &idle);
    event_log_add(stopped ? EV_PUMP_STOPPED : EV_PUMP_END, pump + 1, (int32_t)ran);
}

// Queue runs for schedules starting in the minute beginning at t.
static void check_minute(time_t t)
{
    schedule_t s[MAX_SCHEDULES];
    int due[MAX_SCHEDULES];
    for (int p = 0; p < pump_count(); p++) {
        int n = store_get_schedules(p, s);
        int c = sched_due_at(s, n, t, due);
        for (int i = 0; i < c; i++) {
            const schedule_t *sc = &s[due[i]];
            if (s_last_start[p] >= t) {
                continue;  // already ran for this time (clock moved back)
            }
            if (s_run[p].running || is_queued(p)) {
                event_log_add(EV_SCHEDULE_SKIPPED, p + 1, sc->minute);
                continue;
            }
            enqueue(p, SRC_SCHEDULE, sc->seconds);
        }
    }
}

// Re-run interrupted runs. Runs once, after the settle delay.
static void queue_retries(void)
{
    for (int p = 0; p < pump_count(); p++) {
        busy_t b = store_get_busy(p);
        if (b.active && !s_run[p].running) {
            enqueue(p, SRC_RETRY, b.seconds ? b.seconds : 30);
        }
    }
}

// Queue the latest schedule missed while the power or clock was off.
static void queue_catchups(time_t now)
{
    time_t boundary = (time_t)(s_check_from_min * 60);  // later times are checked live
    time_t window_start = now - CONFIG_WATER_CATCHUP_WINDOW_MIN * 60;
    schedule_t s[MAX_SCHEDULES];

    for (int p = 0; p < pump_count(); p++) {
        time_t last = s_last_start[p];
        if (last == 0 || s_run[p].running || is_queued(p)) {
            continue;  // never ran (fresh install) or already handled
        }
        int n = store_get_schedules(p, s);
        uint16_t secs = 0;
        time_t from = last > window_start - 1 ? last : window_start - 1;
        if (sched_latest_between(s, n, from, boundary, &secs)) {
            enqueue(p, SRC_CATCHUP, secs);
            continue;
        }
        time_t missed = sched_latest_between(s, n, last, window_start, NULL);
        if (missed) {
            struct tm lt;
            localtime_r(&missed, &lt);
            event_log_add(EV_MISSED, p + 1, lt.tm_hour * 60 + lt.tm_min);
        }
    }
}

static void tick(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now_us = esp_timer_get_time();

    for (int p = 0; p < pump_count(); p++) {
        if (s_run[p].running && now_us - s_run[p].start_us >= s_run[p].seconds * US_PER_S) {
            finish_run(p, false);
        }
    }

    if (!s_settled && now_us - s_boot_us >= CONFIG_WATER_SETTLE_DELAY_S * US_PER_S) {
        s_settled = true;
        ESP_LOGI(TAG, "power settled");
        queue_retries();
    }

    if (clock_is_valid()) {
        time_t now = clock_now();
        int64_t min = now / 60;
        if (s_last_min < 0) {
            s_last_min = min - 1;
            s_check_from_min = min;
        } else if (min < s_last_min || min - s_last_min > 2) {
            s_last_min = min;  // clock jumped: don't run the skipped minutes
        }
        while (s_last_min < min) {
            s_last_min++;
            check_minute((time_t)(s_last_min * 60));
        }
        if (s_settled && s_catchup_pending) {
            s_catchup_pending = false;
            queue_catchups(now);
        }
    }

    // Automatic runs wait until power has settled.
    while (s_settled && s_qlen > 0 && running_count() < CONFIG_WATER_MAX_CONCURRENT) {
        pending_t q = s_queue[0];
        memmove(&s_queue[0], &s_queue[1], (s_qlen - 1) * sizeof(pending_t));
        s_qlen--;
        if (!s_run[q.pump].running) {
            start_run(q.pump, (run_source_t)q.source, q.seconds);
        }
    }
    xSemaphoreGive(s_lock);
}

static void controller_task(void *arg)
{
    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    for (;;) {
        tick();
        if (wdt) {
            esp_task_wdt_reset();
        }
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void controller_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_boot_us = esp_timer_get_time();
    for (int p = 0; p < pump_count(); p++) {
        last_run_t last = store_get_last(p);
        busy_t busy = store_get_busy(p);
        s_last_start[p] = busy.active && busy.start > last.start ? busy.start : last.start;
    }
    xTaskCreate(controller_task, "controller", 6144, NULL, 5, NULL);
}

ctrl_result_t controller_start_manual(int pump, int seconds, int *running_pump)
{
    if (seconds < MIN_RUN_S || seconds > CONFIG_WATER_MAX_RUN_S) {
        return CTRL_INVALID;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ctrl_result_t res = CTRL_OK;
    if (s_run[pump].running) {
        res = CTRL_ALREADY_RUNNING;
    } else if (running_count() >= CONFIG_WATER_MAX_CONCURRENT) {
        res = CTRL_BUSY;
        for (int p = 0; p < pump_count(); p++) {
            if (s_run[p].running && running_pump) {
                *running_pump = p;
                break;
            }
        }
    } else {
        dequeue_pump(pump);
        start_run(pump, SRC_MANUAL, seconds);
    }
    xSemaphoreGive(s_lock);
    return res;
}

void controller_stop(int pump)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    dequeue_pump(pump);
    if (s_run[pump].running) {
        finish_run(pump, true);
    }
    xSemaphoreGive(s_lock);
}

void controller_get_status(int pump, pump_status_t *out)
{
    schedule_t s[MAX_SCHEDULES];
    int n = store_get_schedules(pump, s);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const pump_run_t *r = &s_run[pump];
    memset(out, 0, sizeof(*out));
    out->running = r->running;
    out->queued = is_queued(pump);
    if (r->running) {
        out->source = r->source;
        int64_t left = r->seconds - (esp_timer_get_time() - r->start_us) / US_PER_S;
        out->remaining_s = left > 0 ? (int)left : 0;
    }
    xSemaphoreGive(s_lock);

    out->last = store_get_last(pump);
    out->next_run = clock_is_valid() ? sched_next_run(s, n, clock_now()) : 0;
}

bool controller_settling(void)
{
    return !s_settled;
}

void controller_on_clock_set(bool was_valid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_last_min = -1;
    if (!was_valid) {
        s_catchup_pending = true;
    }
    xSemaphoreGive(s_lock);
}
