#include "rtc_ds3231.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "civil.h"

#define DS3231_ADDR   0x68
#define REG_TIME      0x00
#define REG_STATUS    0x0F
#define STATUS_OSF    0x80
#define TIMEOUT_MS    100

static const char *TAG = "ds3231";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static uint8_t from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, TIMEOUT_MS);
}

static esp_err_t write_regs(uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t tmp[8];
    if (len > sizeof(tmp) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    tmp[0] = reg;
    memcpy(tmp + 1, buf, len);
    return i2c_master_transmit(s_dev, tmp, len + 1, TIMEOUT_MS);
}

esp_err_t ds3231_init(int sda_gpio, int scl_gpio)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus init");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "add device");
    return i2c_master_probe(s_bus, DS3231_ADDR, TIMEOUT_MS);
}

esp_err_t ds3231_get_time(time_t *out)
{
    uint8_t r[7];
    ESP_RETURN_ON_ERROR(read_regs(REG_TIME, r, sizeof(r)), TAG, "read time");

    int sec = from_bcd(r[0] & 0x7F);
    int min = from_bcd(r[1] & 0x7F);
    int hour;
    if (r[2] & 0x40) {  // 12-hour mode
        hour = from_bcd(r[2] & 0x1F) % 12 + ((r[2] & 0x20) ? 12 : 0);
    } else {
        hour = from_bcd(r[2] & 0x3F);
    }
    int day = from_bcd(r[4] & 0x3F);
    int month = from_bcd(r[5] & 0x1F);
    int year = 2000 + from_bcd(r[6]);

    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = (time_t)(days_from_civil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec);
    return ESP_OK;
}

esp_err_t ds3231_set_time(time_t t)
{
    int64_t days = t / 86400;
    int secs = (int)(t % 86400);
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    if (y < 2000 || y > 2099) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t r[7] = {
        to_bcd(secs % 60),
        to_bcd((secs / 60) % 60),
        to_bcd(secs / 3600),             // 24-hour mode
        (uint8_t)(weekday_from_days(days) + 1),
        to_bcd(d),
        to_bcd(m),
        to_bcd(y - 2000),
    };
    return write_regs(REG_TIME, r, sizeof(r));
}

esp_err_t ds3231_get_osf(bool *osf)
{
    uint8_t st;
    ESP_RETURN_ON_ERROR(read_regs(REG_STATUS, &st, 1), TAG, "read status");
    *osf = st & STATUS_OSF;
    return ESP_OK;
}

esp_err_t ds3231_clear_osf(void)
{
    uint8_t st;
    ESP_RETURN_ON_ERROR(read_regs(REG_STATUS, &st, 1), TAG, "read status");
    st &= ~STATUS_OSF;
    return write_regs(REG_STATUS, &st, 1);
}
