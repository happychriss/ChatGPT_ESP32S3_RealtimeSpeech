set(COMPONENT_ADD_INCLUDEDIRS ./include)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES audio_sal audio_hal esp_dispatcher display_service driver)

set(CONFIG_ESP_S3_CUSTOM_BOARD esp_s3_custom_board)

if (CONFIG_ESP_S3_CUSTOM_BOARD)
    message(STATUS "Current board name is " CONFIG_ESP_S3_CUSTOM_BOARD)
    list(APPEND COMPONENT_ADD_INCLUDEDIRS ./esp32_s3_custom)
    set(COMPONENT_SRCS
            ./esp32_s3_custom/board.c
            ./esp32_s3_custom/board_pins_config.c
    )
endif()

register_component()
