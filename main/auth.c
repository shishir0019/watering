#include "auth.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "clock.h"
#include "event_log.h"

#define NS              "auth"
#define SALT_LEN        16
#define HASH_LEN        32
#define HASH_ROUNDS     2000
#define MAX_SESSIONS    5
#define MAX_FAILURES    5
#define LOCKOUT_S       60
#define MAX_PASSWORD    64

#if CONFIG_WATER_RESET_PASSWORD
#define RESET_PASSWORD  true
#else
#define RESET_PASSWORD  false
#endif

typedef struct {
    uint8_t salt[SALT_LEN];
    uint8_t hash[HASH_LEN];
} pw_record_t;

typedef struct {
    uint8_t token_hash[HASH_LEN];
    int64_t expires;    // Unix seconds; 0 = set on first use with a valid clock
    uint8_t used;
    uint8_t reserved[7];
} session_t;

static const char *TAG = "auth";

static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static pw_record_t s_pw;
static bool s_default;
static session_t s_sessions[MAX_SESSIONS];
static int s_failures;
static int64_t s_locked_until_us;

// Salted, iterated SHA-256.
static void hash_password(const char *password, const uint8_t salt[SALT_LEN], uint8_t out[HASH_LEN])
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, salt, SALT_LEN);
    mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    mbedtls_sha256_finish(&ctx, out);
    for (int i = 0; i < HASH_ROUNDS; i++) {
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, out, HASH_LEN);
        mbedtls_sha256_update(&ctx, salt, SALT_LEN);
        mbedtls_sha256_finish(&ctx, out);
    }
    mbedtls_sha256_free(&ctx);
}

static void hash_token(const char *token, uint8_t out[HASH_LEN])
{
    mbedtls_sha256((const uint8_t *)token, strlen(token), out, 0);
}

static bool equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

static void save_sessions(void)
{
    if (nvs_set_blob(s_nvs, "sess", s_sessions, sizeof(s_sessions)) != ESP_OK || nvs_commit(s_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "save sessions failed");
    }
}

static void set_password(const char *password, bool is_default)
{
    esp_fill_random(s_pw.salt, SALT_LEN);
    hash_password(password, s_pw.salt, s_pw.hash);
    s_default = is_default;
    nvs_set_blob(s_nvs, "pw", &s_pw, sizeof(s_pw));
    nvs_set_u8(s_nvs, "def", is_default);
    if (nvs_commit(s_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "save password failed");
    }
}

void auth_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(nvs_open(NS, NVS_READWRITE, &s_nvs));

    size_t len = sizeof(s_pw);
    bool have_pw = nvs_get_blob(s_nvs, "pw", &s_pw, &len) == ESP_OK && len == sizeof(s_pw);
    if (!have_pw || RESET_PASSWORD) {
        ESP_LOGW(TAG, "setting initial password");
        set_password(CONFIG_WATER_ADMIN_PASSWORD, true);
        memset(s_sessions, 0, sizeof(s_sessions));
        save_sessions();
        return;
    }
    uint8_t def = 0;
    nvs_get_u8(s_nvs, "def", &def);
    s_default = def;
    len = sizeof(s_sessions);
    if (nvs_get_blob(s_nvs, "sess", s_sessions, &len) != ESP_OK || len != sizeof(s_sessions)) {
        memset(s_sessions, 0, sizeof(s_sessions));
    }
}

static bool verify(const char *password)
{
    if (strlen(password) > MAX_PASSWORD) {
        return false;
    }
    uint8_t h[HASH_LEN];
    hash_password(password, s_pw.salt, h);
    return equal(h, s_pw.hash, HASH_LEN);
}

static int64_t session_expiry(void)
{
    return clock_is_valid() ? (int64_t)clock_now() + AUTH_SESSION_DAYS * 86400LL : 0;
}

auth_result_t auth_login(const char *password, char token_out[AUTH_TOKEN_LEN + 1],
                         int *remaining, int *retry_after)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now_us = esp_timer_get_time();
    if (now_us < s_locked_until_us) {
        *retry_after = (int)((s_locked_until_us - now_us) / 1000000) + 1;
        xSemaphoreGive(s_lock);
        return AUTH_LOCKED;
    }

    if (!verify(password)) {
        auth_result_t res = AUTH_BAD_PASSWORD;
        if (++s_failures >= MAX_FAILURES) {
            s_failures = 0;
            s_locked_until_us = now_us + LOCKOUT_S * 1000000LL;
            *retry_after = LOCKOUT_S;
            res = AUTH_LOCKED;
        } else {
            *remaining = MAX_FAILURES - s_failures;
        }
        xSemaphoreGive(s_lock);
        event_log_add(EV_LOGIN_FAIL, 0, 0);
        return res;
    }
    s_failures = 0;

    uint8_t raw[AUTH_TOKEN_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); i++) {
        sprintf(token_out + i * 2, "%02x", raw[i]);
    }

    // Free slot, else replace the session that expires first.
    int slot = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!s_sessions[i].used) {
            slot = i;
            break;
        }
        if (s_sessions[i].expires < s_sessions[slot].expires) {
            slot = i;
        }
    }
    session_t *s = &s_sessions[slot];
    memset(s, 0, sizeof(*s));
    hash_token(token_out, s->token_hash);
    s->expires = session_expiry();
    s->used = 1;
    save_sessions();
    xSemaphoreGive(s_lock);

    event_log_add(EV_LOGIN_OK, 0, 0);
    return AUTH_OK;
}

static int find_session(const char *token)
{
    if (strlen(token) != AUTH_TOKEN_LEN) {
        return -1;
    }
    uint8_t h[HASH_LEN];
    hash_token(token, h);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].used && equal(h, s_sessions[i].token_hash, HASH_LEN)) {
            return i;
        }
    }
    return -1;
}

bool auth_check(const char *token)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int i = find_session(token);
    bool ok = i >= 0;
    if (ok && clock_is_valid()) {
        session_t *s = &s_sessions[i];
        if (s->expires == 0) {
            s->expires = session_expiry();  // created while the clock was not set
            save_sessions();
        } else if (clock_now() > s->expires) {
            memset(s, 0, sizeof(*s));
            save_sessions();
            ok = false;
        }
    }
    xSemaphoreGive(s_lock);
    return ok;
}

void auth_logout(const char *token)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int i = find_session(token);
    if (i >= 0) {
        memset(&s_sessions[i], 0, sizeof(session_t));
        save_sessions();
    }
    xSemaphoreGive(s_lock);
}

auth_result_t auth_change_password(const char *current, const char *new_password, const char *keep_token)
{
    size_t len = strlen(new_password);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    auth_result_t res = AUTH_OK;
    if (!verify(current)) {
        res = AUTH_BAD_PASSWORD;
    } else if (len < AUTH_MIN_PASSWORD || len > MAX_PASSWORD) {
        res = AUTH_INVALID;
    } else {
        set_password(new_password, false);
        int keep = find_session(keep_token);
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (i != keep) {
                memset(&s_sessions[i], 0, sizeof(session_t));
            }
        }
        save_sessions();
    }
    xSemaphoreGive(s_lock);
    if (res == AUTH_OK) {
        event_log_add(EV_PASSWORD_CHANGED, 0, 0);
    }
    return res;
}

bool auth_is_default_password(void)
{
    return s_default;
}
