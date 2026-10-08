# ESP-IDF discovers component requirements in script mode before generation.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES esp_driver_usb_serial_jtag esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl)
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliPetAssets.cmake")
jelli_generate_pet_assets(pet_assets)
idf_component_register(
    SRCS "main.c" "debug_wire.c" "sound_output.c" "../../../core/debug.c" "../../../core/debug_cheats.c" "../../../core/debug_events.c" "../../../core/debug_sound.c" "../../../core/debug_tunables.c" "../../../core/debug_state.c" "../../../core/engine.c" "../../../core/render.c" "../../../core/animation.c"
         "../../../core/tunables.c" "../../../core/particles.c" "../../../core/sound.c" "../../../core/game.c" "../../../core/events.c" "../../../core/personality.c" "../../../core/game_events.c" "../../../core/game_commands.c" "../../../core/game_time.c"
         "../../../core/game_advance.c" "../../../core/save.c" "../../../core/pet_engine.c"
         "../../../core/pet_atmosphere.c" "../../../core/pet_timing.c" "../../../core/pet_layout.c" "../../../core/pet_actor.c" "../../../core/pet_health.c" "../../../core/pet_ui.c" "../../../core/pet_particles.c" "../../../core/pet_draw.c" "../../../core/pet_render.c" "${pet_assets}"
    INCLUDE_DIRS "../../../include"
    REQUIRES esp_driver_usb_serial_jtag esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
