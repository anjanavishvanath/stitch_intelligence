idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
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
)

