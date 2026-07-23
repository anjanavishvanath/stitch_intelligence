#include "nfc.h"
#include "state.h"
#include "hardware_config.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PN532_ADDR         0x24
#define PN532_PREAMBLE     0x00
#define PN532_STARTCODE1   0x00
#define PN532_STARTCODE2   0xFF
#define PN532_POSTAMBLE    0x00
#define PN532_HOSTTOPN532  0xD4

static const char *TAG = "NFC";
static i2c_master_dev_handle_t g_dev = NULL;

esp_err_t nfc_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PN532_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &g_dev);
    if (err != ESP_OK) return err;

    /* Shared IRQ pin (also used by TCA9534 in the reference wiring). */
    gpio_config_t irq = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pin_bit_mask = (1ULL << NFC_IOX_GPIO),
    };
    gpio_config(&irq);
    ESP_LOGI(TAG, "PN532 attached at 0x%02X (IRQ GPIO %d)", PN532_ADDR, NFC_IOX_GPIO);
    return ESP_OK;
}

static bool pn532_is_ready(void)
{
    if (gpio_get_level(NFC_IOX_GPIO) == 1) return false;   /* neither device active */
    uint8_t status = 0;
    if (i2c_master_receive(g_dev, &status, 1, 20) == ESP_OK) {
        return status == 0x01;
    }
    return false;
}

static esp_err_t pn532_write_command(uint8_t *cmd, uint8_t cmd_len)
{
    uint8_t tx[64];
    uint8_t idx = 0, checksum = 0;
    tx[idx++] = PN532_PREAMBLE;
    tx[idx++] = PN532_STARTCODE1;
    tx[idx++] = PN532_STARTCODE2;
    uint8_t len = cmd_len + 1;
    tx[idx++] = len;
    tx[idx++] = ~len + 1;
    tx[idx++] = PN532_HOSTTOPN532;
    checksum += PN532_HOSTTOPN532;
    for (uint8_t i = 0; i < cmd_len; i++) {
        tx[idx++] = cmd[i];
        checksum += cmd[i];
    }
    tx[idx++] = ~checksum + 1;
    tx[idx++] = PN532_POSTAMBLE;
    return i2c_master_transmit(g_dev, tx, idx, -1);
}

static esp_err_t pn532_read_response(uint8_t *reply, uint8_t expected_len)
{
    uint8_t tries = 0;
    while (!pn532_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (++tries > 50) return ESP_ERR_TIMEOUT;
    }
    uint8_t raw[64];
    uint8_t total = 1 + 5 + expected_len + 2;
    esp_err_t err = i2c_master_receive(g_dev, raw, total, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;
    memcpy(reply, &raw[6], expected_len);
    return ESP_OK;
}

static void nfc_task(void *pv)
{
    ESP_LOGI(TAG, "waking PN532...");
    uint8_t wake[4] = { 0x55, 0x00, 0x00, 0x00 };
    i2c_master_transmit(g_dev, wake, 4, -1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* SAMConfiguration: normal, IRQ not used for signalling */
    uint8_t sam_cmd[3] = { 0x14, 0x01, 0x14 };
    if (pn532_write_command(sam_cmd, 3) == ESP_OK) {
        uint8_t sam_reply[8];
        if (pn532_read_response(sam_reply, 2) == ESP_OK) {
            ESP_LOGI(TAG, "SAM configured");
        }
    }

    /* InListPassiveTarget: poll for 1 Type-A target */
    uint8_t poll_cmd[3] = { 0x4A, 0x01, 0x00 };
    while (1) {
        if (pn532_write_command(poll_cmd, 3) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(30));
            uint8_t reply[32];
            if (pn532_read_response(reply, 20) == ESP_OK) {
                uint8_t targets = reply[2];
                if (targets > 0) {
                    uint8_t uid_len = reply[7];
                    char hex[32] = {0};
                    char byte[4];
                    for (int i = 0; i < uid_len && i < 10; i++) {
                        snprintf(byte, sizeof(byte), "%02X ", reply[8 + i]);
                        strcat(hex, byte);
                    }
                    state_set_nfc(hex);
                } else {
                    state_set_nfc("SEARCHING...");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

void nfc_start_task(void)
{
    xTaskCreatePinnedToCore(nfc_task, "nfc", 4096, NULL, 4, NULL, 0);
}
