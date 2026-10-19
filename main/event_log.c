#include "event_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "clock.h"

#define BASE_PATH  "/log"
#define LOG_FILE   BASE_PATH "/events.bin"
#define LOG_MAGIC  0x574C4F47u  // "WLOG"

typedef struct {
    uint32_t magic;
    uint16_t head;      // next slot to write
    uint16_t count;
} log_header_t;

static const char *TAG = "event_log";

static SemaphoreHandle_t s_lock;
static log_header_t s_hdr;
static bool s_ok;

static long slot_offset(int slot)
{
    return (long)(sizeof(log_header_t) + slot * sizeof(log_record_t));
}

static bool create_file(void)
{
    FILE *f = fopen(LOG_FILE, "wb");
    if (!f) {
        return false;
    }
    s_hdr = (log_header_t){ .magic = LOG_MAGIC };
    log_record_t empty = { 0 };
    bool ok = fwrite(&s_hdr, sizeof(s_hdr), 1, f) == 1;
    for (int i = 0; ok && i < LOG_CAPACITY; i++) {
        ok = fwrite(&empty, sizeof(empty), 1, f) == 1;
    }
    fclose(f);
    return ok;
}

esp_err_t event_log_init(void)
{
    s_lock = xSemaphoreCreateMutex();

    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = "log",
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    FILE *f = fopen(LOG_FILE, "rb");
    bool valid = f && fread(&s_hdr, sizeof(s_hdr), 1, f) == 1 && s_hdr.magic == LOG_MAGIC
                 && s_hdr.head < LOG_CAPACITY && s_hdr.count <= LOG_CAPACITY;
    if (f) {
        fclose(f);
    }
    if (!valid && !create_file()) {
        ESP_LOGE(TAG, "cannot create log file");
        return ESP_FAIL;
    }
    s_ok = true;
    return ESP_OK;
}

void event_log_add(event_type_t type, int pump, int32_t value)
{
    log_record_t rec = {
        .time = clock_is_valid() ? (uint32_t)clock_now() : 0,
        .type = (uint8_t)type,
        .pump = (uint8_t)pump,
        .value = value,
    };
    ESP_LOGI(TAG, "%s pump=%d value=%ld", event_name(type), pump, (long)value);
    if (!s_ok) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    FILE *f = fopen(LOG_FILE, "r+b");
    if (f) {
        log_header_t next = s_hdr;
        next.head = (s_hdr.head + 1) % LOG_CAPACITY;
        if (next.count < LOG_CAPACITY) {
            next.count++;
        }
        bool ok = fseek(f, slot_offset(s_hdr.head), SEEK_SET) == 0
                  && fwrite(&rec, sizeof(rec), 1, f) == 1
                  && fseek(f, 0, SEEK_SET) == 0
                  && fwrite(&next, sizeof(next), 1, f) == 1;
        fclose(f);
        if (ok) {
            s_hdr = next;
        } else {
            ESP_LOGE(TAG, "write failed");
        }
    }
    xSemaphoreGive(s_lock);
}

int event_log_read(int pump, log_record_t *out, int max)
{
    if (!s_ok || max <= 0) {
        return 0;
    }
    log_record_t *all = malloc(LOG_CAPACITY * sizeof(log_record_t));
    if (!all) {
        return 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    log_header_t hdr = s_hdr;
    FILE *f = fopen(LOG_FILE, "rb");
    bool ok = f && fseek(f, slot_offset(0), SEEK_SET) == 0
              && fread(all, sizeof(log_record_t), LOG_CAPACITY, f) == LOG_CAPACITY;
    if (f) {
        fclose(f);
    }
    xSemaphoreGive(s_lock);

    int n = 0;
    for (int i = 0; ok && i < hdr.count && n < max; i++) {
        const log_record_t *r = &all[(hdr.head - 1 - i + LOG_CAPACITY) % LOG_CAPACITY];
        if (pump == 0 || r->pump == pump) {
            out[n++] = *r;
        }
    }
    free(all);
    return n;
}

const char *event_name(event_type_t type)
{
    switch (type) {
    case EV_BOOT:             return "BOOT";
    case EV_PUMP_START:       return "PUMP_START";
    case EV_PUMP_END:         return "PUMP_END";
    case EV_PUMP_STOPPED:     return "PUMP_STOPPED";
    case EV_SCHEDULE_SKIPPED: return "SCHEDULE_SKIPPED";
    case EV_MISSED:           return "MISSED";
    case EV_SCHEDULE_CHANGED: return "SCHEDULE_CHANGED";
    case EV_PUMP_RENAMED:     return "PUMP_RENAMED";
    case EV_LOGIN_OK:         return "LOGIN_OK";
    case EV_LOGIN_FAIL:       return "LOGIN_FAIL";
    case EV_PASSWORD_CHANGED: return "PASSWORD_CHANGED";
    case EV_RTC_FAULT:        return "RTC_FAULT";
    case EV_TIME_SET:         return "TIME_SET";
    default:                  return "UNKNOWN";
    }
}
