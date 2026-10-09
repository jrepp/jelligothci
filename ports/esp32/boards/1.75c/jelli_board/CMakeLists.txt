# Waveshare ESP32-S3-Touch-AMOLED-1.75C. The BSP comes from idf_component.yml.
# Pins the BSP does not expose, read by the agent's phase 0 probes (RFC-003): the
# PWR key level through a BSS138 on GPIO3, and the CO5300 tearing-effect output,
# which the BSP's init commands enable (0x35), on GPIO13.
idf_component_register()
target_compile_definitions(${COMPONENT_LIB} INTERFACE JELLI_BOARD_LABEL="ESP32-S3-Touch-AMOLED-1.75C"
                           JELLI_BOARD_PWR_KEY_GPIO=3 JELLI_BOARD_LCD_TE_GPIO=13)
