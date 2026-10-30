## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

if(UNIX)
  if(APPLE)
    set(_ibrt_runtime_patterns
      "${IBRT_BEXT_LIBRARY_DIR}/libospray.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libospray.*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libospray_module_cpu*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/librkcommon*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libembree*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libtbb*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libispcrt*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libopenvkl*.dylib")
  else()
    set(_ibrt_runtime_patterns
      "${IBRT_BEXT_LIBRARY_DIR}/libospray.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libospray_module_cpu.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/librkcommon.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libembree.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libtbb.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libispcrt.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libopenvkl.so*")
  endif()

  file(GLOB _ibrt_local_ospray_runtime_libs
    LIST_DIRECTORIES FALSE
    ${_ibrt_runtime_patterns})
endif()

if(WIN32)
  set(_ibrt_windows_runtime_search_dirs
    "${IBRT_BEXT_RUNTIME_DIR}"
    "${IBRT_BRLCAD_RUNTIME_DIR}"
    "$<TARGET_FILE_DIR:Qt6::Core>")

  set(_ibrt_runtime_dependency_tool)
  set(_ibrt_runtime_dependency_command)
  if(CMAKE_OBJDUMP AND EXISTS "${CMAKE_OBJDUMP}")
    set(_ibrt_runtime_dependency_tool objdump)
    set(_ibrt_runtime_dependency_command "${CMAKE_OBJDUMP}")
  elseif(MSVC AND CMAKE_LINKER)
    get_filename_component(_ibrt_msvc_tool_dir "${CMAKE_LINKER}" DIRECTORY)
    find_program(_ibrt_dumpbin_executable
      NAMES dumpbin dumpbin.exe
      HINTS "${_ibrt_msvc_tool_dir}"
      NO_DEFAULT_PATH)
    if(_ibrt_dumpbin_executable)
      set(_ibrt_runtime_dependency_tool dumpbin)
      set(_ibrt_runtime_dependency_command "${_ibrt_dumpbin_executable}")
    endif()
  endif()
endif()

function(ibrt_deploy_runtime_bundle target)
  if(WIN32)
    set(_runtime_modules)
    set(_runtime_search_dirs ${_ibrt_windows_runtime_search_dirs})
    foreach(_module_target IN LISTS ARGN)
      if(TARGET ${_module_target})
        add_dependencies(${target} ${_module_target})
        list(APPEND _runtime_modules "$<TARGET_FILE:${_module_target}>")
        list(APPEND _runtime_search_dirs "$<TARGET_FILE_DIR:${_module_target}>")
      endif()
    endforeach()

    list(REMOVE_DUPLICATES _runtime_search_dirs)
    list(JOIN _runtime_modules "@IBRT_LIST_SEPARATOR@" _encoded_runtime_modules)
    list(JOIN _runtime_search_dirs "@IBRT_LIST_SEPARATOR@" _encoded_runtime_search_dirs)

    set(_runtime_tool_arguments)
    if(_ibrt_runtime_dependency_tool)
      list(APPEND _runtime_tool_arguments
        "-DIBRT_RUNTIME_DEPENDENCY_TOOL=${_ibrt_runtime_dependency_tool}"
        "-DIBRT_RUNTIME_DEPENDENCY_COMMAND=${_ibrt_runtime_dependency_command}")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
        "-DIBRT_RUNTIME_EXECUTABLES=$<TARGET_FILE:${target}>"
        "-DIBRT_RUNTIME_MODULES=${_encoded_runtime_modules}"
        "-DIBRT_RUNTIME_SEARCH_DIRECTORIES=${_encoded_runtime_search_dirs}"
        "-DIBRT_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:${target}>"
        "-DIBRT_RUNTIME_DEPENDENCY_PLATFORM=windows+pe"
        ${_runtime_tool_arguments}
        -P "${PROJECT_SOURCE_DIR}/cmake/IBRTDeployRuntimeDependencies.cmake"
      COMMENT "Resolving runtime libraries for ${target}..."
      VERBATIM)
  elseif(UNIX)
    foreach(_lib IN LISTS _ibrt_local_ospray_runtime_libs)
      get_filename_component(_lib_name "${_lib}" NAME)
      if(IS_SYMLINK "${_lib}")
        file(READ_SYMLINK "${_lib}" _lib_link_target)
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:${target}>/${_lib_name}"
          COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${_lib_link_target}"
            "$<TARGET_FILE_DIR:${target}>/${_lib_name}"
          COMMENT "Creating local OSPRay runtime symlink ${_lib_name} for ${target}...")
      else()
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_lib}"
            "$<TARGET_FILE_DIR:${target}>/${_lib_name}"
          COMMENT "Copying local OSPRay runtime library ${_lib_name} for ${target}...")
      endif()
    endforeach()
  endif()
endfunction()
