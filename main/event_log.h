#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    EV_BOOT = 1,
    EV_PUMP_START,          // value: run_source_t
    EV_PUMP_END,            // value: seconds run
    EV_PUMP_STOPPED,        // value: seconds run
    EV_SCHEDULE_SKIPPED,    // value: schedule minute of day
    EV_MISSED,              // value: schedule minute of day
    EV_SCHEDULE_CHANGED,    // value: 1 added, 2 edited, 3 deleted
    EV_PUMP_RENAMED,
    EV_LOGIN_OK,
    EV_LOGIN_FAIL,
    EV_PASSWORD_CHANGED,
    EV_RTC_FAULT,           // value: 1 RTC responding, 0 not found
    EV_TIME_SET,            // value: change in seconds
} event_type_t;

typedef struct {
    uint32_t time;      // Unix seconds, 0 = clock not set
    uint8_t  type;      // event_type_t
    uint8_t  pump;      // 1-based, 0 = system
    uint16_t reserved;
    int32_t  value;
} log_record_t;

#define LOG_CAPACITY 500

esp_err_t event_log_init(void);
void event_log_add(event_type_t type, int pump, int32_t value);

// Newest first. pump: 1-based, 0 = all events. Returns the number written to out[].
int event_log_read(int pump, log_record_t *out, int max);

const char *event_name(event_type_t type);
