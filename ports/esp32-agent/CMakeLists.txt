cmake_minimum_required(VERSION 3.20)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" PROJECT_VER LIMIT_COUNT 1)

# The voice agent app (RFC-003). It shares the pet's board wrappers (ADR-011) and
# flash layout (ADR-013) but is its own ESP-IDF project with its own sdkconfig and
# lock files, installed in ota_0 beside the pet in factory.
set(JELLI_ESP_BOARD "1.75" CACHE STRING "Waveshare board variant to build for")
set(jelli_esp_boards "1.75" "1.75c")
set_property(CACHE JELLI_ESP_BOARD PROPERTY STRINGS ${jelli_esp_boards})
if(NOT JELLI_ESP_BOARD IN_LIST jelli_esp_boards)
    message(FATAL_ERROR "Unknown JELLI_ESP_BOARD '${JELLI_ESP_BOARD}'; expected one of: ${jelli_esp_boards}")
endif()
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../esp32/boards/${JELLI_ESP_BOARD}")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.${JELLI_ESP_BOARD}.lock")
# The pet refuses to boot an ota_0 image under any other project name (app_switch.c).
project(jelli_agent)
