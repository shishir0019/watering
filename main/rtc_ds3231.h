#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

// The DS3231 stores UTC.

esp_err_t ds3231_init(int sda_gpio, int scl_gpio);
esp_err_t ds3231_get_time(time_t *out);
esp_err_t ds3231_set_time(time_t t);

// Oscillator-stop flag: set when the clock stopped (coin cell dead or missing).
esp_err_t ds3231_get_osf(bool *osf);
esp_err_t ds3231_clear_osf(void);
