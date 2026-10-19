#pragma once

// Owns all relays: schedules, run queue, catch-up/retry after power loss, manual control.
// All pump indices are 0-based.

#include <stdbool.h>
#include <time.h>
#include "water_types.h"

typedef enum {
    CTRL_OK,
    CTRL_INVALID,           // duration out of range
    CTRL_ALREADY_RUNNING,   // this pump is running
    CTRL_BUSY,              // max concurrent pumps already running
} ctrl_result_t;

typedef struct {
    bool         running;
    bool         queued;
    run_source_t source;        // of the current run
    int          remaining_s;
    last_run_t   last;          // last completed run
    time_t       next_run;      // 0 if none or clock not valid
} pump_status_t;

void controller_init(void);

// On CTRL_BUSY, *running_pump is set to a pump that is running.
ctrl_result_t controller_start_manual(int pump, int seconds, int *running_pump);
void controller_stop(int pump);

void controller_get_status(int pump, pump_status_t *out);
bool controller_settling(void);

// clock_set_cb_t
void controller_on_clock_set(bool was_valid);
