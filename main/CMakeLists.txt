idf_component_register(
    SRCS "main.c"
         "relay.c"
         "rtc_ds3231.c"
         "clock.c"
         "store.c"
         "scheduler.c"
         "controller.c"
         "event_log.c"
         "auth.c"
         "wifi.c"
         "web_server.c"
         "api.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES driver esp_wifi esp_netif esp_event esp_timer esp_http_server
             esp_app_format nvs_flash json mbedtls lwip
)
