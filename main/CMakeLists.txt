idf_component_register(
    SRCS "main.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES display ble_pad driver esp_timer nvs_flash
)
