#pragma once

#include "driver/i2c_master.h"

/*
 * TCA9534 8-bit I/O expander on the shared I2C bus.
 * Lower 4 bits = inputs (buttons).
 * Upper 4 bits = outputs (LEDs) — shifted from `mask & 0x0F` on write.
 */

esp_err_t expander_init(i2c_master_bus_handle_t bus);
esp_err_t expander_read_switches(uint8_t *out_mask);   /* returns lower 4 bits */
esp_err_t expander_write_leds(uint8_t mask);           /* only bottom 4 bits used */
