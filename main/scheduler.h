#pragma once

// Pure schedule calculations in local time (uses TZ). No ESP-IDF dependencies.

#include <time.h>
#include "water_types.h"

// Earliest enabled start time strictly after `now`; 0 if none within 8 days.
time_t sched_next_run(const schedule_t *s, int n, time_t now);

// Latest enabled start time t with from < t < to; 0 if none.
// Looks back at most 8 days from `to`. Sets *seconds to that schedule's duration.
time_t sched_latest_between(const schedule_t *s, int n, time_t from, time_t to, uint16_t *seconds);

// Indices of enabled schedules that start in the local minute containing `t`.
// Returns the number of indices written to out[] (out must hold n entries).
int sched_due_at(const schedule_t *s, int n, time_t t, int *out);
