# Compile validated four-channel MODs with host Python, including cross builds.
function(jelli_music_data output)
  get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  file(GLOB modules "${root}/content/music/*.mod")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${modules} "${root}/content/music.json" "${root}/tools/audio/build_music.py" "${root}/tools/jelli-art/music_mod.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${root}/tools/audio/build_music.py"
    --root "${root}" --output "${output}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "MOD compilation failed: ${error}")
  endif()
endfunction()
