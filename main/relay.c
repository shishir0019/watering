#include "relay.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "water_types.h"

#if CONFIG_WATER_RELAY_ACTIVE_LOW
#define LEVEL_ON  0
#else
#define LEVEL_ON  1
#endif
#define LEVEL_OFF (!LEVEL_ON)

static const char *TAG = "relay";

static const gpio_num_t s_pins[MAX_PUMPS] = {
    CONFIG_WATER_RELAY1_GPIO,
    CONFIG_WATER_RELAY2_GPIO,
    CONFIG_WATER_RELAY3_GPIO,
    CONFIG_WATER_RELAY4_GPIO,
};

void relay_init(void)
{
    for (int i = 0; i < CONFIG_WATER_PUMP_COUNT; i++) {
        // Latch the OFF level before enabling the output driver to avoid a pulse.
        gpio_set_level(s_pins[i], LEVEL_OFF);
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        gpio_set_level(s_pins[i], LEVEL_OFF);
    }
}

void relay_set(int pump, bool on)
{
    if (pump < 0 || pump >= CONFIG_WATER_PUMP_COUNT) {
        return;
    }
    gpio_set_level(s_pins[pump], on ? LEVEL_ON : LEVEL_OFF);
    ESP_LOGI(TAG, "pump %d %s", pump + 1, on ? "ON" : "OFF");
}
