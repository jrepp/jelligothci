# ESP-IDF discovers component requirements in script mode before generation.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl)
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/JelliPetAssets.cmake")
jelli_generate_pet_assets(pet_assets)
idf_component_register(
    SRCS "main.c" "../../../core/engine.c" "../../../core/render.c" "../../../core/animation.c"
         "../../../core/game.c" "../../../core/game_commands.c" "../../../core/game_time.c"
         "../../../core/game_advance.c" "../../../core/save.c" "../../../core/pet_engine.c"
         "../../../core/pet_ui.c" "../../../core/pet_render.c" "${pet_assets}"
    INCLUDE_DIRS "../../../include"
    REQUIRES esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
