#include "web_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "api.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "web";

static esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    // EMBED_TXTFILES appends a NUL terminator.
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

void web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 16;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        return;
    }

    const httpd_uri_t index = { .uri = "/", .method = HTTP_GET, .handler = handle_index };
    httpd_register_uri_handler(server, &index);
    api_register(server);
}
