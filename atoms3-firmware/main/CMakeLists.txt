idf_component_register(
    SRCS
        "app_main.c"
        "m5atoms3_hal.c"
        "m5atoms3_gfx.cpp"
        "trigger_proto.c"
        "trigger_ble.c"
        "trigger_state.c"
        "trigger_ui.c"
    PRIV_REQUIRES
        bt
        nvs_flash
        esp_lcd
        driver
    INCLUDE_DIRS "."
)

# Compile flag — lets the AtomS3-specific paths in shared HAL/UI code light up.
option(M5ATOMS3_BUILD "Build for M5 AtomS3 (ESP32-S3FN8)" OFF)
if(M5ATOMS3_BUILD)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC M5ATOMS3)
endif()
