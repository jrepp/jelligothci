# Waveshare ESP32-S3-Touch-AMOLED-1.75, SKU 31261. The BSP comes from idf_component.yml.
idf_component_register()
# JELLI_BOARD_ID is the short name the voice agent reports in loki/1's hello (at most 16 bytes).
target_compile_definitions(${COMPONENT_LIB} INTERFACE JELLI_BOARD_LABEL="ESP32-S3-Touch-AMOLED-1.75"
                           JELLI_BOARD_ID="1.75")
