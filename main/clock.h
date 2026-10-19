#pragma once

#include <stdbool.h>
#include <time.h>

// Called after the clock is set. was_valid: the clock was valid before the change.
typedef void (*clock_set_cb_t)(bool was_valid);

// Set TZ, read the RTC into the system clock and decide whether the time is valid.
void clock_init(void);
void clock_set_callback(clock_set_cb_t cb);

bool clock_is_valid(void);
bool clock_rtc_ok(void);
time_t clock_now(void);

// Set the time (UTC) from the phone. Ignored if the clock is valid and within 30 s.
void clock_set(time_t t);

// Start SNTP (call once Wi-Fi has an IP). Safe to call repeatedly.
void clock_start_sntp(void);
