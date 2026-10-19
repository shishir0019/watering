#include "wifi.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "clock.h"

#define RECONNECT_DELAY_US (5 * 1000000LL)

static const char *TAG = "wifi";

static volatile bool s_connected;
static esp_timer_handle_t s_reconnect_timer;

#define TRY(call) do { \
    esp_err_t err_ = (call); \
    if (err_ != ESP_OK) { ESP_LOGE(TAG, "%s: %s", #call, esp_err_to_name(err_)); return; } \
} while (0)

static void reconnect(void *arg)
{
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        esp_timer_stop(s_reconnect_timer);
        esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_US);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        clock_start_sntp();
    }
}

void wifi_start(void)
{
    if (strlen(CONFIG_WATER_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no SSID configured; Wi-Fi disabled");
        return;
    }

    TRY(esp_netif_init());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, CONFIG_WATER_HOSTNAME);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    TRY(esp_wifi_init(&init_cfg));

    const esp_timer_create_args_t timer_args = { .callback = reconnect, .name = "wifi_reconnect" };
    TRY(esp_timer_create(&timer_args, &s_reconnect_timer));
    TRY(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    TRY(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, CONFIG_WATER_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_WATER_WIFI_PASSWORD, sizeof(cfg.sta.password));
    TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    TRY(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    TRY(esp_wifi_start());
}

bool wifi_connected(void)
{
    return s_connected;
}

int wifi_rssi(void)
{
    wifi_ap_record_t ap;
    if (!s_connected || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    return ap.rssi;
}
