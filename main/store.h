#pragma once

// Pump settings and run state, cached in RAM and persisted in NVS.
// All pump indices are 0-based.

#include <stdbool.h>
#include "esp_err.h"
#include "water_types.h"

typedef struct {
    int64_t  start;     // Unix seconds
    uint16_t seconds;   // requested duration
    uint8_t  active;    // 1 while the pump is running
    uint8_t  source;    // run_source_t
} busy_t;

esp_err_t store_init(void);
int store_pump_count(void);

void store_get_name(int pump, char out[PUMP_NAME_LEN]);
esp_err_t store_set_name(int pump, const char *name);

// Returns the number of schedules copied to out[MAX_SCHEDULES].
int store_get_schedules(int pump, schedule_t *out);

// Errors: ESP_ERR_INVALID_ARG invalid fields, ESP_ERR_NO_MEM limit reached,
// ESP_ERR_INVALID_STATE same time on an overlapping day, ESP_ERR_NOT_FOUND unknown id.
esp_err_t store_add_schedule(int pump, schedule_t *s);   // assigns s->id
esp_err_t store_update_schedule(int pump, const schedule_t *s);
esp_err_t store_delete_schedule(int pump, uint8_t id);

last_run_t store_get_last(int pump);
void store_set_last(int pump, const last_run_t *last);

busy_t store_get_busy(int pump);
void store_set_busy(int pump, const busy_t *busy);
