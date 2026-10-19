#pragma once

#include <stdint.h>

#define MAX_PUMPS     4
#define MAX_SCHEDULES 8
#define PUMP_NAME_LEN 32
#define MIN_RUN_S     5
#define DAYS_ALL      0x7F   // bit0 = Monday ... bit6 = Sunday

typedef enum {
    SRC_NONE = 0,
    SRC_SCHEDULE,
    SRC_MANUAL,
    SRC_CATCHUP,
    SRC_RETRY,
} run_source_t;

typedef struct {
    uint8_t  id;        // 1..255, unique per pump
    uint8_t  days;      // weekday mask, see DAYS_ALL
    uint8_t  enabled;
    uint8_t  reserved;
    uint16_t minute;    // local minute of day, 0..1439
    uint16_t seconds;   // run duration
} schedule_t;

typedef struct {
    int64_t  start;     // Unix seconds, 0 = never
    uint16_t seconds;
    uint8_t  source;    // run_source_t
    uint8_t  reserved;
} last_run_t;

const char *source_name(run_source_t src);
