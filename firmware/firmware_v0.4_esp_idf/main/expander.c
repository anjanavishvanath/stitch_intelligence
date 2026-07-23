#include "expander.h"
#include "esp_log.h"

#define TCA9534_ADDR         0x20
#define TCA9534_REG_INPUT    0x00
#define TCA9534_REG_OUTPUT   0x01
#define TCA9534_REG_CONFIG   0x03

static const char *TAG = "EXP";
static i2c_master_dev_handle_t g_dev = NULL;

esp_err_t expander_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TCA9534_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &g_dev);
    if (err != ESP_OK) return err;

    /* Config: 0x0F = P0..P3 inputs, P4..P7 outputs (matches reference wiring) */
    uint8_t config[2]      = { TCA9534_REG_CONFIG, 0x0F };
    uint8_t initial_out[2] = { TCA9534_REG_OUTPUT, 0x00 };
    ESP_ERROR_CHECK(i2c_master_transmit(g_dev, config,      2, -1));
    ESP_ERROR_CHECK(i2c_master_transmit(g_dev, initial_out, 2, -1));
    ESP_LOGI(TAG, "TCA9534 initialised at 0x%02X", TCA9534_ADDR);
    return ESP_OK;
}

esp_err_t expander_read_switches(uint8_t *out_mask)
{
    uint8_t reg = TCA9534_REG_INPUT, val = 0;
    esp_err_t err = i2c_master_transmit_receive(g_dev, &reg, 1, &val, 1, -1);
    if (err == ESP_OK) *out_mask = val & 0x0F;
    return err;
}

esp_err_t expander_write_leds(uint8_t mask)
{
    uint8_t tx[2] = { TCA9534_REG_OUTPUT, (mask << 4) & 0xF0 };
    return i2c_master_transmit(g_dev, tx, 2, -1);
}
