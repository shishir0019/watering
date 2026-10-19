#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "auth.h"
#include "clock.h"
#include "controller.h"
#include "event_log.h"
#include "store.h"
#include "wifi.h"

#define MAX_BODY        1024
#define LOG_LIMIT_MAX   200
#define LOG_LIMIT_DEF   50
#define PUMPS_PREFIX    "/api/pumps/"

#define S201 "201 Created"
#define S401 "401 Unauthorized"
#define S409 "409 Conflict"
#define S415 "415 Unsupported Media Type"
#define S429 "429 Too Many Requests"
#define S500 "500 Internal Server Error"

// ---------------------------------------------------------------------------
// Helpers

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, body ? body : "{}");
    free(body);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", msg);
    return send_json(req, status, j);
}

static esp_err_t send_ok(httpd_req_t *req)
{
    return send_json(req, HTTPD_200, cJSON_CreateObject());
}

static bool get_token(httpd_req_t *req, char token[AUTH_TOKEN_LEN + 1])
{
    size_t len = AUTH_TOKEN_LEN + 1;
    return httpd_req_get_cookie_val(req, "sid", token, &len) == ESP_OK;
}

// Sends 401 and returns false if there is no valid session.
static bool require_auth(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (get_token(req, token) && auth_check(token)) {
        return true;
    }
    send_error(req, S401, "Login required");
    return false;
}

// Reads a JSON object body. Requires Content-Type: application/json (CSRF protection).
// On failure sends an error response and returns NULL.
static cJSON *read_json(httpd_req_t *req)
{
    char ct[40] = "";
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof(ct));
    if ((err != ESP_OK && err != ESP_ERR_HTTPD_RESULT_TRUNC) || strncmp(ct, "application/json", 16) != 0) {
        send_error(req, S415, "Content-Type must be application/json");
        return NULL;
    }
    if (req->content_len > MAX_BODY) {
        send_error(req, HTTPD_400, "Request too large");
        return NULL;
    }
    char buf[MAX_BODY + 1];
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            send_error(req, HTTPD_400, "Read error");
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    cJSON *j = got ? cJSON_Parse(buf) : cJSON_CreateObject();
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        send_error(req, HTTPD_400, "Invalid JSON");
        return NULL;
    }
    return j;
}

static const char *get_string(const cJSON *j, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

// "/api/pumps/2/schedules/5" -> pump 1 (0-based), rest "/schedules/5". Query string is dropped.
static bool parse_pump_path(httpd_req_t *req, int *pump, char *rest, size_t rest_len)
{
    const char *p = req->uri + strlen(PUMPS_PREFIX);
    char *end;
    long id = strtol(p, &end, 10);
    if (end == p || id < 1 || id > store_pump_count()) {
        return false;
    }
    size_t n = strcspn(end, "?");
    if (n >= rest_len) {
        return false;
    }
    memcpy(rest, end, n);
    rest[n] = '\0';
    *pump = (int)id - 1;
    return true;
}

static void add_time(cJSON *obj, const char *key, time_t t)
{
    if (t > 0) {
        cJSON_AddNumberToObject(obj, key, (double)t);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

// ---------------------------------------------------------------------------
// JSON builders

static cJSON *schedule_json(const schedule_t *s)
{
    char time[8];
    snprintf(time, sizeof(time), "%02d:%02d", s->minute / 60, s->minute % 60);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", s->id);
    cJSON_AddStringToObject(o, "time", time);
    cJSON_AddNumberToObject(o, "seconds", s->seconds);
    cJSON *days = cJSON_AddArrayToObject(o, "days");
    for (int d = 0; d < 7; d++) {
        if (s->days & (1u << d)) {
            cJSON_AddItemToArray(days, cJSON_CreateNumber(d + 1));
        }
    }
    cJSON_AddBoolToObject(o, "enabled", s->enabled);
    return o;
}

static cJSON *pump_json(int pump, bool with_schedules)
{
    char name[PUMP_NAME_LEN];
    pump_status_t st;
    store_get_name(pump, name);
    controller_get_status(pump, &st);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", pump + 1);
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddBoolToObject(o, "running", st.running);
    cJSON_AddNumberToObject(o, "remaining_s", st.remaining_s);
    if (st.running) {
        cJSON_AddStringToObject(o, "source", source_name(st.source));
    } else {
        cJSON_AddNullToObject(o, "source");
    }
    cJSON_AddBoolToObject(o, "queued", st.queued);
    if (st.last.start > 0) {
        cJSON *last = cJSON_AddObjectToObject(o, "last_run");
        cJSON_AddNumberToObject(last, "start", (double)st.last.start);
        cJSON_AddNumberToObject(last, "seconds", st.last.seconds);
        cJSON_AddStringToObject(last, "source", source_name(st.last.source));
    } else {
        cJSON_AddNullToObject(o, "last_run");
    }
    add_time(o, "next_run", st.next_run);

    if (with_schedules) {
        schedule_t s[MAX_SCHEDULES];
        int n = store_get_schedules(pump, s);
        cJSON *arr = cJSON_AddArrayToObject(o, "schedules");
        for (int i = 0; i < n; i++) {
            cJSON_AddItemToArray(arr, schedule_json(&s[i]));
        }
    }
    return o;
}

// Returns an error message, or NULL if valid.
static const char *parse_schedule(const cJSON *j, schedule_t *s)
{
    const char *time = get_string(j, "time");
    const cJSON *secs = cJSON_GetObjectItemCaseSensitive(j, "seconds");
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(j, "days");
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(j, "enabled");

    int h, m;
    char extra;
    if (!time || sscanf(time, "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return "Start time must be HH:MM";
    }
    if (!cJSON_IsNumber(secs) || secs->valueint < MIN_RUN_S || secs->valueint > CONFIG_WATER_MAX_RUN_S) {
        return "Duration is out of range";
    }
    if (!cJSON_IsArray(days) || cJSON_GetArraySize(days) == 0) {
        return "Select at least one day";
    }
    uint8_t mask = 0;
    const cJSON *d;
    cJSON_ArrayForEach(d, days) {
        if (!cJSON_IsNumber(d) || d->valueint < 1 || d->valueint > 7) {
            return "Days must be 1 (Mon) to 7 (Sun)";
        }
        mask |= 1u << (d->valueint - 1);
    }
    if (enabled && !cJSON_IsBool(enabled)) {
        return "Enabled must be true or false";
    }

    s->minute = (uint16_t)(h * 60 + m);
    s->seconds = (uint16_t)secs->valueint;
    s->days = mask;
    s->enabled = enabled ? cJSON_IsTrue(enabled) : 1;
    return NULL;
}

static esp_err_t send_store_error(httpd_req_t *req, esp_err_t err)
{
    switch (err) {
    case ESP_ERR_INVALID_ARG:   return send_error(req, HTTPD_400, "Invalid schedule");
    case ESP_ERR_NO_MEM:        return send_error(req, S409, "Maximum 8 schedules per pump");
    case ESP_ERR_INVALID_STATE: return send_error(req, S409, "A schedule already exists at this time");
    case ESP_ERR_NOT_FOUND:     return send_error(req, HTTPD_404, "Schedule not found");
    default:                    return send_error(req, S500, "Storage error");
    }
}

// ---------------------------------------------------------------------------
// Authentication

static esp_err_t handle_login(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    const char *pw = get_string(j, "password");
    if (!pw) {
        cJSON_Delete(j);
        return send_error(req, HTTPD_400, "Password required");
    }
    char token[AUTH_TOKEN_LEN + 1];
    int remaining = 0, retry_after = 0;
    auth_result_t res = auth_login(pw, token, &remaining, &retry_after);
    cJSON_Delete(j);

    if (res == AUTH_LOCKED) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "error", "Too many attempts");
        cJSON_AddNumberToObject(o, "retry_after", retry_after);
        return send_json(req, S429, o);
    }
    if (res != AUTH_OK) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "error", "Wrong password");
        cJSON_AddNumberToObject(o, "remaining", remaining);
        return send_json(req, S401, o);
    }

    char cookie[128];
    snprintf(cookie, sizeof(cookie), "sid=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict",
             token, AUTH_SESSION_DAYS * 86400);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "default_password", auth_is_default_password());
    return send_json(req, HTTPD_200, o);
}

static esp_err_t handle_logout(httpd_req_t *req)
{
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    cJSON_Delete(j);
    char token[AUTH_TOKEN_LEN + 1];
    if (get_token(req, token)) {
        auth_logout(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    return send_ok(req);
}

static esp_err_t handle_password(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    const char *cur = get_string(j, "current");
    const char *new_pw = get_string(j, "new");
    char token[AUTH_TOKEN_LEN + 1] = "";
    get_token(req, token);
    auth_result_t res = (cur && new_pw) ? auth_change_password(cur, new_pw, token) : AUTH_INVALID;
    cJSON_Delete(j);

    switch (res) {
    case AUTH_OK:           return send_ok(req);
    case AUTH_BAD_PASSWORD: return send_error(req, S401, "Current password is wrong");
    default:                return send_error(req, HTTPD_400, "New password must be 8-64 characters");
    }
}

// ---------------------------------------------------------------------------
// Pumps and schedules

static esp_err_t handle_pumps(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    cJSON *arr = cJSON_CreateArray();
    for (int p = 0; p < store_pump_count(); p++) {
        cJSON_AddItemToArray(arr, pump_json(p, false));
    }
    return send_json(req, HTTPD_200, arr);
}

static esp_err_t handle_pump_get(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    int pump;
    char rest[32];
    if (!parse_pump_path(req, &pump, rest, sizeof(rest)) || rest[0] != '\0') {
        return send_error(req, HTTPD_404, "Pump not found");
    }
    return send_json(req, HTTPD_200, pump_json(pump, true));
}

static esp_err_t handle_pump_patch(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    int pump;
    char rest[32];
    if (!parse_pump_path(req, &pump, rest, sizeof(rest)) || rest[0] != '\0') {
        return send_error(req, HTTPD_404, "Pump not found");
    }
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    const char *name = get_string(j, "name");
    esp_err_t err = name ? store_set_name(pump, name) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(j);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, HTTPD_400, "Name must be 1-31 characters");
    }
    if (err != ESP_OK) {
        return send_error(req, S500, "Storage error");
    }
    event_log_add(EV_PUMP_RENAMED, pump + 1, 0);
    return send_json(req, HTTPD_200, pump_json(pump, true));
}

static esp_err_t pump_start(httpd_req_t *req, int pump, const cJSON *j)
{
    const cJSON *secs = cJSON_GetObjectItemCaseSensitive(j, "seconds");
    int seconds = cJSON_IsNumber(secs) ? secs->valueint : 30;
    int running = -1;
    char name[PUMP_NAME_LEN];
    char msg[80];

    switch (controller_start_manual(pump, seconds, &running)) {
    case CTRL_OK:
        return send_json(req, HTTPD_200, pump_json(pump, false));
    case CTRL_INVALID:
        snprintf(msg, sizeof(msg), "Duration must be %d-%d seconds", MIN_RUN_S, CONFIG_WATER_MAX_RUN_S);
        return send_error(req, HTTPD_400, msg);
    case CTRL_ALREADY_RUNNING:
        return send_error(req, S409, "This pump is already running");
    default:
        store_get_name(running >= 0 ? running : 0, name);
        snprintf(msg, sizeof(msg), "%s is running. Stop it first.", name);
        return send_error(req, S409, msg);
    }
}

static esp_err_t handle_pump_post(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    int pump;
    char rest[32];
    if (!parse_pump_path(req, &pump, rest, sizeof(rest))) {
        return send_error(req, HTTPD_404, "Pump not found");
    }
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }

    esp_err_t res;
    if (strcmp(rest, "/start") == 0) {
        res = pump_start(req, pump, j);
    } else if (strcmp(rest, "/stop") == 0) {
        controller_stop(pump);
        res = send_json(req, HTTPD_200, pump_json(pump, false));
    } else if (strcmp(rest, "/schedules") == 0) {
        schedule_t s = { 0 };
        const char *msg = parse_schedule(j, &s);
        if (msg) {
            res = send_error(req, HTTPD_400, msg);
        } else {
            esp_err_t err = store_add_schedule(pump, &s);
            if (err != ESP_OK) {
                res = send_store_error(req, err);
            } else {
                event_log_add(EV_SCHEDULE_CHANGED, pump + 1, 1);
                res = send_json(req, S201, schedule_json(&s));
            }
        }
    } else {
        res = send_error(req, HTTPD_404, "Not found");
    }
    cJSON_Delete(j);
    return res;
}

// "/schedules/{sid}" -> sid, or -1.
static int parse_schedule_id(const char *rest)
{
    const char *prefix = "/schedules/";
    if (strncmp(rest, prefix, strlen(prefix)) != 0) {
        return -1;
    }
    char *end;
    long id = strtol(rest + strlen(prefix), &end, 10);
    return (*end == '\0' && id >= 1 && id <= 255) ? (int)id : -1;
}

static esp_err_t handle_schedule_put(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    int pump, sid;
    char rest[32];
    if (!parse_pump_path(req, &pump, rest, sizeof(rest)) || (sid = parse_schedule_id(rest)) < 0) {
        return send_error(req, HTTPD_404, "Not found");
    }
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    schedule_t s = { .id = (uint8_t)sid };
    const char *msg = parse_schedule(j, &s);
    cJSON_Delete(j);
    if (msg) {
        return send_error(req, HTTPD_400, msg);
    }
    esp_err_t err = store_update_schedule(pump, &s);
    if (err != ESP_OK) {
        return send_store_error(req, err);
    }
    event_log_add(EV_SCHEDULE_CHANGED, pump + 1, 2);
    return send_json(req, HTTPD_200, schedule_json(&s));
}

static esp_err_t handle_schedule_delete(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    int pump, sid;
    char rest[32];
    if (!parse_pump_path(req, &pump, rest, sizeof(rest)) || (sid = parse_schedule_id(rest)) < 0) {
        return send_error(req, HTTPD_404, "Not found");
    }
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    cJSON_Delete(j);
    esp_err_t err = store_delete_schedule(pump, (uint8_t)sid);
    if (err != ESP_OK) {
        return send_store_error(req, err);
    }
    event_log_add(EV_SCHEDULE_CHANGED, pump + 1, 3);
    return send_ok(req);
}

// ---------------------------------------------------------------------------
// Logs and system

static esp_err_t handle_logs(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    int pump = 0, limit = LOG_LIMIT_DEF;
    char query[64], val[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "pump", val, sizeof(val)) == ESP_OK) {
            pump = atoi(val);
        }
        if (httpd_query_key_value(query, "limit", val, sizeof(val)) == ESP_OK) {
            limit = atoi(val);
        }
    }
    if (pump < 0 || pump > store_pump_count()) {
        return send_error(req, HTTPD_404, "Pump not found");
    }
    if (limit < 1 || limit > LOG_LIMIT_MAX) {
        limit = LOG_LIMIT_DEF;
    }

    log_record_t *recs = malloc(limit * sizeof(log_record_t));
    if (!recs) {
        return send_error(req, S500, "Out of memory");
    }
    int n = event_log_read(pump, recs, limit);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        const log_record_t *r = &recs[i];
        cJSON *o = cJSON_CreateObject();
        add_time(o, "time", r->time);
        cJSON_AddStringToObject(o, "event", event_name(r->type));
        cJSON_AddNumberToObject(o, "pump", r->pump);
        cJSON_AddNumberToObject(o, "value", r->value);
        if (r->type == EV_PUMP_START) {
            cJSON_AddStringToObject(o, "source", source_name(r->value));
        }
        cJSON_AddItemToArray(arr, o);
    }
    free(recs);
    return send_json(req, HTTPD_200, arr);
}

static esp_err_t handle_system(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    cJSON *o = cJSON_CreateObject();
    add_time(o, "now", clock_is_valid() ? clock_now() : 0);
    cJSON_AddBoolToObject(o, "time_valid", clock_is_valid());
    cJSON_AddBoolToObject(o, "rtc_ok", clock_rtc_ok());
    cJSON_AddStringToObject(o, "tz", CONFIG_WATER_TZ);
    cJSON_AddNumberToObject(o, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddBoolToObject(o, "settling", controller_settling());
    cJSON_AddNumberToObject(o, "rssi", wifi_rssi());
    cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
    cJSON_AddBoolToObject(o, "default_password", auth_is_default_password());
    cJSON_AddNumberToObject(o, "pump_count", store_pump_count());
    cJSON_AddNumberToObject(o, "min_run_s", MIN_RUN_S);
    cJSON_AddNumberToObject(o, "max_run_s", CONFIG_WATER_MAX_RUN_S);
    return send_json(req, HTTPD_200, o);
}

static esp_err_t handle_time(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    cJSON *j = read_json(req);
    if (!j) {
        return ESP_OK;
    }
    const cJSON *unix_time = cJSON_GetObjectItemCaseSensitive(j, "unix");
    bool ok = cJSON_IsNumber(unix_time) && unix_time->valuedouble > 1.6e9 && unix_time->valuedouble < 4.1e9;
    time_t t = ok ? (time_t)unix_time->valuedouble : 0;
    cJSON_Delete(j);
    if (!ok) {
        return send_error(req, HTTPD_400, "Invalid time");
    }
    clock_set(t);
    return send_ok(req);
}

// ---------------------------------------------------------------------------

void api_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/login",    .method = HTTP_POST,   .handler = handle_login },
        { .uri = "/api/logout",   .method = HTTP_POST,   .handler = handle_logout },
        { .uri = "/api/password", .method = HTTP_POST,   .handler = handle_password },
        { .uri = "/api/pumps",    .method = HTTP_GET,    .handler = handle_pumps },
        { .uri = "/api/pumps/*",  .method = HTTP_GET,    .handler = handle_pump_get },
        { .uri = "/api/pumps/*",  .method = HTTP_PATCH,  .handler = handle_pump_patch },
        { .uri = "/api/pumps/*",  .method = HTTP_POST,   .handler = handle_pump_post },
        { .uri = "/api/pumps/*",  .method = HTTP_PUT,    .handler = handle_schedule_put },
        { .uri = "/api/pumps/*",  .method = HTTP_DELETE, .handler = handle_schedule_delete },
        { .uri = "/api/logs",     .method = HTTP_GET,    .handler = handle_logs },
        { .uri = "/api/system",   .method = HTTP_GET,    .handler = handle_system },
        { .uri = "/api/time",     .method = HTTP_POST,   .handler = handle_time },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE("api", "register %s: %s", routes[i].uri, esp_err_to_name(err));
        }
    }
}
