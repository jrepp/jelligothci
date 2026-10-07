idf_component_register(
    SRCS "main.c" "../../../core/engine.c" "../../../core/render.c"
    INCLUDE_DIRS "../../../include"
    REQUIRES esp_timer esp_psram esp32_s3_touch_amoled_1_75 lvgl
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
