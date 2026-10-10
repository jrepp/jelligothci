function(jelli_generate_pet_assets output_variable)
  get_filename_component(jelli_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  if(WIN32)
    find_program(jelli_uv NAMES uv REQUIRED)
  else()
    set(jelli_uv "${jelli_root}/scripts/uv")
  endif()
  set(JELLI_ASSET_UV "${jelli_uv}" PARENT_SCOPE)
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/jelli_assets.c")
  file(GLOB_RECURSE art_inputs CONFIGURE_DEPENDS "${jelli_root}/assets/slice/*.png")
  add_custom_command(
    OUTPUT "${generated}"
    COMMAND "${jelli_uv}" run --python 3.12
            "${jelli_root}/tools/assets/embed_slice.py" --output "${generated}"
    DEPENDS "${jelli_root}/tools/assets/embed_slice.py"
            "${jelli_root}/tools/assets/build_slice.py"
            "${jelli_root}/tools/assets/sprite_geometry.py"
            "${jelli_root}/tools/assets/creature_data.py"
            "${jelli_root}/assets/slice/assets.json" "${jelli_root}/content/pets.json" "${jelli_root}/content/creatures.json" "${jelli_root}/toolchain.env" ${art_inputs}
    COMMENT "Embedding validated slice artwork"
    VERBATIM
  )
  set(${output_variable} "${generated}" PARENT_SCOPE)
endfunction()
