#include "store.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define NS "water"

typedef struct {
    char       name[PUMP_NAME_LEN];
    schedule_t sched[MAX_SCHEDULES];
    int        count;
    last_run_t last;
    busy_t     busy;
} pump_data_t;

static const char *TAG = "store";

static pump_data_t s_pumps[MAX_PUMPS];
static nvs_handle_t s_nvs;
static SemaphoreHandle_t s_lock;

static void make_key(char key[16], const char *prefix, int pump)
{
    snprintf(key, 16, "%s%d", prefix, pump);
}

static void save_blob(const char *prefix, int pump, const void *data, size_t len)
{
    char key[16];
    make_key(key, prefix, pump);
    esp_err_t err = nvs_set_blob(s_nvs, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save %s: %s", key, esp_err_to_name(err));
    }
}

static void load_blob(const char *prefix, int pump, void *data, size_t *len)
{
    char key[16];
    make_key(key, prefix, pump);
    if (nvs_get_blob(s_nvs, key, data, len) != ESP_OK) {
        *len = 0;
    }
}

static void save_schedules(int pump)
{
    save_blob("sch", pump, s_pumps[pump].sched, s_pumps[pump].count * sizeof(schedule_t));
}

static void set_defaults(void)
{
    // Pump 1 waters three times a day, matching the original 8-hour design.
    static const uint16_t minutes[] = { 6 * 60, 14 * 60, 22 * 60 };
    pump_data_t *p = &s_pumps[0];
    for (int i = 0; i < 3; i++) {
        p->sched[i] = (schedule_t){
            .id = (uint8_t)(i + 1), .days = DAYS_ALL, .enabled = 1, .minute = minutes[i], .seconds = 30,
        };
    }
    p->count = 3;
    save_schedules(0);
    nvs_set_u8(s_nvs, "init", 1);
    nvs_commit(s_nvs);
}

esp_err_t store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }
    s_lock = xSemaphoreCreateMutex();

    for (int i = 0; i < CONFIG_WATER_PUMP_COUNT; i++) {
        pump_data_t *p = &s_pumps[i];
        char key[16];
        size_t len = sizeof(p->name);
        make_key(key, "name", i);
        if (nvs_get_str(s_nvs, key, p->name, &len) != ESP_OK) {
            snprintf(p->name, sizeof(p->name), "Pump %d", i + 1);
        }
        len = sizeof(p->sched);
        load_blob("sch", i, p->sched, &len);
        p->count = len / sizeof(schedule_t);
        len = sizeof(p->last);
        load_blob("last", i, &p->last, &len);
        len = sizeof(p->busy);
        load_blob("busy", i, &p->busy, &len);
    }

    uint8_t init = 0;
    if (nvs_get_u8(s_nvs, "init", &init) != ESP_OK || !init) {
        set_defaults();
    }
    return ESP_OK;
}

int store_pump_count(void)
{
    return CONFIG_WATER_PUMP_COUNT;
}

void store_get_name(int pump, char out[PUMP_NAME_LEN])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(out, s_pumps[pump].name, PUMP_NAME_LEN);
    xSemaphoreGive(s_lock);
}

esp_err_t store_set_name(int pump, const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= PUMP_NAME_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    make_key(key, "name", pump);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_pumps[pump].name, name, len + 1);
    esp_err_t err = nvs_set_str(s_nvs, key, name);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    xSemaphoreGive(s_lock);
    return err;
}

int store_get_schedules(int pump, schedule_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_pumps[pump].count;
    memcpy(out, s_pumps[pump].sched, n * sizeof(schedule_t));
    xSemaphoreGive(s_lock);
    return n;
}

static bool schedule_valid(const schedule_t *s)
{
    return s->minute < 1440
           && s->seconds >= MIN_RUN_S && s->seconds <= CONFIG_WATER_MAX_RUN_S
           && (s->days & DAYS_ALL) && !(s->days & ~DAYS_ALL);
}

static bool time_taken(const pump_data_t *p, const schedule_t *s)
{
    for (int i = 0; i < p->count; i++) {
        const schedule_t *o = &p->sched[i];
        if (o->id != s->id && o->minute == s->minute && (o->days & s->days)) {
            return true;
        }
    }
    return false;
}

static int find_schedule(const pump_data_t *p, uint8_t id)
{
    for (int i = 0; i < p->count; i++) {
        if (p->sched[i].id == id) {
            return i;
        }
    }
    return -1;
}

esp_err_t store_add_schedule(int pump, schedule_t *s)
{
    if (!schedule_valid(s)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pump_data_t *p = &s_pumps[pump];
    esp_err_t err = ESP_OK;
    if (p->count >= MAX_SCHEDULES) {
        err = ESP_ERR_NO_MEM;
    } else {
        s->id = 0;
        for (int id = 1; id <= 255 && !s->id; id++) {
            if (find_schedule(p, (uint8_t)id) < 0) {
                s->id = (uint8_t)id;
            }
        }
        if (time_taken(p, s)) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            s->reserved = 0;
            p->sched[p->count++] = *s;
            save_schedules(pump);
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t store_update_schedule(int pump, const schedule_t *s)
{
    if (!schedule_valid(s)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pump_data_t *p = &s_pumps[pump];
    int i = find_schedule(p, s->id);
    esp_err_t err = ESP_OK;
    if (i < 0) {
        err = ESP_ERR_NOT_FOUND;
    } else if (time_taken(p, s)) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        p->sched[i] = *s;
        p->sched[i].reserved = 0;
        save_schedules(pump);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t store_delete_schedule(int pump, uint8_t id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pump_data_t *p = &s_pumps[pump];
    int i = find_schedule(p, id);
    esp_err_t err = ESP_OK;
    if (i < 0) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        memmove(&p->sched[i], &p->sched[i + 1], (p->count - i - 1) * sizeof(schedule_t));
        p->count--;
        save_schedules(pump);
    }
    xSemaphoreGive(s_lock);
    return err;
}

last_run_t store_get_last(int pump)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    last_run_t l = s_pumps[pump].last;
    xSemaphoreGive(s_lock);
    return l;
}

void store_set_last(int pump, const last_run_t *last)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pumps[pump].last = *last;
    save_blob("last", pump, last, sizeof(*last));
    xSemaphoreGive(s_lock);
}

busy_t store_get_busy(int pump)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    busy_t b = s_pumps[pump].busy;
    xSemaphoreGive(s_lock);
    return b;
}

void store_set_busy(int pump, const busy_t *busy)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pumps[pump].busy = *busy;
    save_blob("busy", pump, busy, sizeof(*busy));
    xSemaphoreGive(s_lock);
}
