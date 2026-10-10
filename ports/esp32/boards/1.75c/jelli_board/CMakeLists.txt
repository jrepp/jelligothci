# Waveshare ESP32-S3-Touch-AMOLED-1.75C. The BSP comes from idf_component.yml.
# Pins the BSP does not expose, read by the agent's phase 0 probes (RFC-003): the
# PWR key level through a BSS138 on GPIO3, and the CO5300 tearing-effect output,
# which the BSP's init commands enable (0x35), on GPIO13. The agent's programmable
# side button, BOOT, is GPIO0 (ADR-016).
idf_component_register()
# JELLI_BOARD_ID is the short name the voice agent reports in loki/1's hello (at most 16 bytes).
target_compile_definitions(${COMPONENT_LIB} INTERFACE JELLI_BOARD_LABEL="ESP32-S3-Touch-AMOLED-1.75C"
                           JELLI_BOARD_ID="1.75c" JELLI_BOARD_PWR_KEY_GPIO=3
                           JELLI_BOARD_LCD_TE_GPIO=13 JELLI_BOARD_BOOT_KEY_GPIO=0)
