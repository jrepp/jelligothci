cmake_minimum_required(VERSION 3.20)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" PROJECT_VER LIMIT_COUNT 1)

# Each supported board is a `jelli_board` wrapper component that pins its BSP.
# The 1.75 keeps the original sdkconfig and lock paths; other boards keep their
# generated sdkconfig in the build directory and their own tracked lock file.
set(JELLI_ESP_BOARD "1.75" CACHE STRING "Waveshare board variant to build for")
set(jelli_esp_boards "1.75" "1.75c")
set_property(CACHE JELLI_ESP_BOARD PROPERTY STRINGS ${jelli_esp_boards})
if(NOT JELLI_ESP_BOARD IN_LIST jelli_esp_boards)
    message(FATAL_ERROR "Unknown JELLI_ESP_BOARD '${JELLI_ESP_BOARD}'; expected one of: ${jelli_esp_boards}")
endif()
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards/${JELLI_ESP_BOARD}")
if(NOT JELLI_ESP_BOARD STREQUAL "1.75")
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT JELLI_ESP_BOARD STREQUAL "1.75")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.${JELLI_ESP_BOARD}.lock")
endif()
project(jelligotchi)
