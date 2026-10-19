#include "esp_event.h"
#include "esp_log.h"
#include "auth.h"
#include "clock.h"
#include "controller.h"
#include "event_log.h"
#include "relay.h"
#include "store.h"
#include "web_server.h"
#include "wifi.h"

static const char *TAG = "main";

void app_main(void)
{
    // Pumps OFF before anything else.
    relay_init();

    ESP_ERROR_CHECK(store_init());
    if (event_log_init() != ESP_OK) {
        ESP_LOGE(TAG, "event log unavailable; continuing without it");
    }
    clock_init();
    event_log_add(EV_BOOT, 0, 0);
    if (!clock_is_valid()) {
        event_log_add(EV_RTC_FAULT, 0, clock_rtc_ok());
    }
    auth_init();

    controller_init();
    clock_set_callback(controller_on_clock_set);

    // Networking is optional: watering continues if any of this fails.
    if (esp_event_loop_create_default() == ESP_OK) {
        wifi_start();
        web_server_start();
    } else {
        ESP_LOGE(TAG, "event loop failed; website disabled");
    }
}
