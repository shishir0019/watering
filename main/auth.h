#pragma once

#include <stdbool.h>

#define AUTH_TOKEN_LEN      32   // hex characters
#define AUTH_SESSION_DAYS   30
#define AUTH_MIN_PASSWORD   8

typedef enum {
    AUTH_OK,
    AUTH_BAD_PASSWORD,
    AUTH_LOCKED,
    AUTH_INVALID,       // new password too short or too long
} auth_result_t;

void auth_init(void);

// On AUTH_OK writes a new session token to token_out.
// On AUTH_BAD_PASSWORD sets *remaining attempts; on AUTH_LOCKED sets *retry_after seconds.
auth_result_t auth_login(const char *password, char token_out[AUTH_TOKEN_LEN + 1],
                         int *remaining, int *retry_after);
bool auth_check(const char *token);
void auth_logout(const char *token);

// Logs out every session except keep_token.
auth_result_t auth_change_password(const char *current, const char *new_password, const char *keep_token);

bool auth_is_default_password(void);
