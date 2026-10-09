# Embeds the validated slice artwork as immutable C arrays (flash rodata on ESP32).
# JELLI_ASSET_SOURCE selects the asset directory; it defaults to assets/slice and
# exists so integration and size measurements can embed a candidate set without
# touching the shipped manifest.
function(jelli_generate_pet_assets output_variable)
  get_filename_component(jelli_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  if(WIN32)
    find_program(jelli_uv NAMES uv REQUIRED)
  else()
    set(jelli_uv "${jelli_root}/scripts/uv")
  endif()
  set(JELLI_ASSET_UV "${jelli_uv}" PARENT_SCOPE)
  set(JELLI_ASSET_SOURCE "${jelli_root}/assets/slice" CACHE PATH "Directory containing assets.json and its PNGs")
  get_filename_component(asset_source "${JELLI_ASSET_SOURCE}" ABSOLUTE)
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/jelli_assets.c")
  file(GLOB_RECURSE art_inputs CONFIGURE_DEPENDS "${asset_source}/*.png")
  add_custom_command(
    OUTPUT "${generated}"
    COMMAND "${jelli_uv}" run --python 3.12
            "${jelli_root}/tools/assets/embed_slice.py" --source "${asset_source}"
            --output "${generated}"
    DEPENDS "${jelli_root}/tools/assets/embed_slice.py"
            "${jelli_root}/tools/assets/build_slice.py"
            "${jelli_root}/tools/assets/sprite_geometry.py"
            "${asset_source}/assets.json" "${jelli_root}/toolchain.env" ${art_inputs}
    COMMENT "Embedding validated slice artwork"
    VERBATIM
  )
  set(${output_variable} "${generated}" PARENT_SCOPE)
endfunction()
