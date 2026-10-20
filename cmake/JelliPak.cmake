# Provisional artwork compiler. Keep this an explicit target until the renderer
# consumes Jellipak through the residency layer. Running it always revalidates
# sources and the native decoder; the compiler preserves identical output bytes.
add_custom_target(jellipak_assets
  COMMAND "${JELLI_ASSET_UV}" run --python 3.12
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets/jellipak.py" build
          --output "${CMAKE_CURRENT_BINARY_DIR}/generated/jellipak.jpak"
          --report "${CMAKE_CURRENT_BINARY_DIR}/generated/jellipak-report.json"
          --verifier "$<TARGET_FILE:jelli_jellipak_verify>"
          --decoded-limit 16384
  DEPENDS jelli_jellipak_verify
  COMMENT "Packing artwork and verifying Jellipak with the native reader"
  VERBATIM
)
