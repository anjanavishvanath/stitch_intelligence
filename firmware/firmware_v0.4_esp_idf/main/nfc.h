#pragma once

#include "driver/i2c_master.h"

/*
 * PN532 NFC reader on the shared I2C bus + interrupt pin NFC_IOX_GPIO.
 * The task polls for Type-A targets every ~400 ms and pushes the UID
 * (or "SEARCHING...") into state_set_nfc().
 */

esp_err_t nfc_init(i2c_master_bus_handle_t bus);
void      nfc_start_task(void);
