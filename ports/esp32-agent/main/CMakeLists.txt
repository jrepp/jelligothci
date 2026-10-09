idf_component_register(
    SRCS "agent_main.c" "boot.c" "commands.c" "console_wire.c" "probe.c" "screen.c"
         "audio.c" "commands_spike.c" "link.c" "settings.c" "wifi.c"
         "../../../core/agent/args.c" "../../../core/agent/console.c"
         "../../../core/agent/pcm.c" "../../../core/agent/pmu.c"
    INCLUDE_DIRS "../../../include"
    REQUIRES app_update esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_pcnt
             esp_driver_usb_serial_jtag esp_event esp_lcd esp_netif esp_psram esp_ringbuf
             esp_timer esp_websocket_client esp_wifi jelli_board nvs_flash spi_flash
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(${COMPONENT_LIB} PRIVATE JELLI_VERSION_LABEL="v${PROJECT_VER}")
