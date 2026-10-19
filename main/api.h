#pragma once

#include "esp_http_server.h"

// Registers all /api/* handlers.
void api_register(httpd_handle_t server);
