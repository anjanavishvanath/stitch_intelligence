idf_component_register(
    SRCS "lcd_ui.c" "indicator.c" "nfc.c" "expander.c" "state.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        # existing — network, activation, MQTT, segmentation
        esp_driver_gpio
        esp_timer
        cjson
        esp_wifi
        nvs_flash
        esp_netif
        esp_event
        esp_http_client
        esp-tls
        mqtt
        # peripherals we now drive
        driver          # i2c_master + rmt (led_strip uses RMT under the hood)
        esp_lcd         # ST7789V + i80 bus + panel_io
)

