function(jelli_generate_pet_assets output_variable)
  get_filename_component(jelli_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  if(WIN32)
    message(FATAL_ERROR "Pet artwork build currently requires the macOS/Linux uv wrapper; use JELLI_BUILD_SDL=OFF for portable core tests")
  endif()
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/jelli_assets.c")
  file(GLOB_RECURSE art_inputs CONFIGURE_DEPENDS "${jelli_root}/assets/slice/*.png")
  add_custom_command(
    OUTPUT "${generated}"
    COMMAND "${jelli_root}/scripts/uv" run --python 3.12
            "${jelli_root}/tools/assets/embed_slice.py" --output "${generated}"
    DEPENDS "${jelli_root}/tools/assets/embed_slice.py"
            "${jelli_root}/tools/assets/build_slice.py"
            "${jelli_root}/assets/slice/assets.json" "${jelli_root}/toolchain.env" ${art_inputs}
    COMMENT "Embedding validated slice artwork"
    VERBATIM
  )
  set(${output_variable} "${generated}" PARENT_SCOPE)
endfunction()
