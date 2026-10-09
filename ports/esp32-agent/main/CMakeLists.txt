idf_component_register(
    SRCS "agent_main.c" "boot.c" "commands.c" "console_wire.c" "probe.c" "screen.c"
         "../../../core/agent/console.c" "../../../core/agent/pmu.c"
    INCLUDE_DIRS "../../../include"
    REQUIRES app_update esp_driver_gpio esp_driver_i2c esp_driver_pcnt esp_driver_usb_serial_jtag
             esp_lcd esp_psram spi_flash jelli_board
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(${COMPONENT_LIB} PRIVATE JELLI_VERSION_LABEL="v${PROJECT_VER}")
