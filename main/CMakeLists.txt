set(ota_sources "")
if(CONFIG_SATELLITE_OTA)
    list(APPEND ota_sources "ota.c")
endif()

idf_component_register(
    SRCS ${ota_sources} "main.c" "board.c" "audio.c" "link.c" "leds.c" "button.c" "wifi.c" "config.c" "discovery.c" "portal.c" "echo.c"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_wifi esp_event esp_netif esp_timer esp_app_format esp_ringbuf nvs_flash json mbedtls esp_http_server
)
