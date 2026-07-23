#pragma once

#include "driver/i2c_master.h"

/*
 * Brings up the ST7789V (i80 8-bit parallel), attaches TSC2007 touch to the
 * shared I2C bus, initialises LVGL, builds the dashboard, and pins the GUI
 * task to core 1. All UI updates happen inside an LVGL timer that snapshots
 * state via state_get() and polls the TCA9534 for switch positions.
 */

void lcd_ui_start(i2c_master_bus_handle_t i2c_bus);
