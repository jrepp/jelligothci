# ESP-IDF discovers component requirements in script mode before generation.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES esp_lcd esp_lvgl_adapter esp_wifi esp_event esp_netif esp_http_client app_update mbedtls nvs_flash esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl)
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliPetAssets.cmake")
jelli_generate_pet_assets(pet_assets)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliCollection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliActivities.cmake")
jelli_activities_data(activities_data)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliPotty.cmake")
jelli_potty_data(potty_data)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliBehaviors.cmake")
jelli_behaviors_data(behaviors_data)
jelli_collection_data(collection_data)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliFood.cmake")
jelli_food_data(food_data)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliExercise.cmake")
jelli_exercise_data(exercise_data)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliWake.cmake")
jelli_wake_data(wake_data)
idf_component_register(
    SRCS "${wake_data}" "../../../core/wake.c" "../../../core/creature.c" "${activities_data}" "../../../core/activities.c" "${potty_data}" "../../../core/potty.c" "${behaviors_data}" "../../../core/behavior.c" "../../../core/save_tail.c" "${exercise_data}" "${food_data}" "${collection_data}" "../../../core/collection_migrate.c" "../../../core/collection.c" "../../../core/nutrition.c" "network.c" "network_config.c" "network_storage.c" "network_worker.c" "network_ota.c" "main.c" "display_output.c" "render_benchmark.c" "display_transfer.c" "session.c" "debug_wire.c" "sound_output.c" "../../../core/debug.c" "../../../core/debug_clock.c" "../../../core/debug_habits.c" "../../../core/debug_cheats.c" "../../../core/debug_events.c" "../../../core/debug_sound.c" "../../../core/debug_tunables.c" "../../../core/debug_state.c" "../../../core/gesture.c" "../../../core/engine.c" "../../../core/render.c" "../../../core/animation.c"
         "../../../core/prizes.c" "../../../core/habits.c" "../../../core/sleep_log.c" "../../../core/tunables.c" "../../../core/particles.c" "../../../core/sound.c" "../../../core/game.c" "../../../core/events.c" "../../../core/personality.c" "../../../core/game_events.c" "../../../core/game_api.c" "../../../core/game_commands.c" "../../../core/game_time.c"
         "../../../core/game_advance.c" "../../../core/save.c" "../../../core/pet_engine.c" "../../../core/pet_rewards.c"
         "../../../core/pet_bubbles.c" "../../../core/pet_atmosphere.c" "../../../core/pet_timing.c" "../../../core/pet_actions.c" "../../../core/pet_clock.c" "../../../core/pet_layout.c" "../../../core/pet_actor.c" "../../../core/pet_health.c" "../../../core/pet_feedback.c" "../../../core/pet_ui.c" "../../../core/pet_particles.c" "../../../core/pet_food.c" "../../../core/pet_collection.c" "../../../core/pet_gallery.c" "../../../core/asset_source.c" "../../../core/pet_canvas.c" "../../../core/pet_menu.c" "../../../core/pet_behavior_draw.c" "../../../core/pet_draw.c" "../../../core/pet_render.c" "${pet_assets}"
    INCLUDE_DIRS "../../../include"
    PRIV_INCLUDE_DIRS "${JELLI_ASSET_IDS_DIR}"
    REQUIRES esp_lcd esp_lvgl_adapter esp_wifi esp_event esp_netif esp_http_client app_update mbedtls nvs_flash esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

target_compile_definitions(${COMPONENT_LIB} PRIVATE JELLI_VERSION_LABEL="v${PROJECT_VER}")
