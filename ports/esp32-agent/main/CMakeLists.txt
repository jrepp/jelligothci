# ESP-IDF discovers component requirements in script mode before generation.
set(agent_requires app_update esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_pcnt
    esp_driver_usb_serial_jtag esp_event esp_lcd esp_netif esp_psram esp_ringbuf esp_timer
    esp_websocket_client esp_wifi jelli_board nvs_flash spi_flash)
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES ${agent_requires})
    return()
endif()
# Only the unicorn's poses (and the font): the pet's whole art set would not fit ota_0.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliPetAssets.cmake")
jelli_generate_pet_assets(agent_assets NAME agent_assets KEYS creatures.unicorn-)
idf_component_register(
    SRCS "agent_main.c" "boot.c" "commands.c" "console_wire.c" "probe.c" "screen.c"
         "audio.c" "commands_spike.c" "link.c" "render_bench.c" "settings.c" "wifi.c"
         "../../../core/agent/args.c" "../../../core/agent/console.c" "../../../core/agent/pcm.c"
         "../../../core/agent/pmu.c" "../../../core/agent/present.c"
         "../../../core/agent/scene.c" "../../../core/asset_source.c"
         "${agent_assets}"
    INCLUDE_DIRS "../../../include"
    REQUIRES ${agent_requires}
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(${COMPONENT_LIB} PRIVATE JELLI_VERSION_LABEL="v${PROJECT_VER}")
