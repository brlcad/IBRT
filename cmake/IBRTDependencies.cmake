## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

include_guard(GLOBAL)

function(ibrt_require_directory var description)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "${var} is not set. Pass -D${var}=<path> (${description}).")
  endif()

  if(NOT IS_DIRECTORY "${${var}}")
    message(FATAL_ERROR "${var} does not exist: ${${var}}")
  endif()
endfunction()

function(ibrt_get_imported_location out_var target)
  foreach(_config IN ITEMS RELEASE RELWITHDEBINFO MINSIZEREL DEBUG NOCONFIG)
    get_target_property(_location "${target}" "IMPORTED_LOCATION_${_config}")
    if(_location)
      set(${out_var} "${_location}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  get_target_property(_location "${target}" IMPORTED_LOCATION)
  if(_location)
    set(${out_var} "${_location}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(ibrt_collect_apple_absolute_dependency_dirs out_var)
  if(NOT APPLE)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  find_program(IBRT_OTOOL_EXECUTABLE otool)
  if(NOT IBRT_OTOOL_EXECUTABLE)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  set(_probe_targets ospray::ospray ospray::ospray_module_cpu rkcommon::rkcommon embree)
  set(_absolute_dirs)

  foreach(_target IN LISTS _probe_targets)
    if(NOT TARGET "${_target}")
      continue()
    endif()

    ibrt_get_imported_location(_target_location "${_target}")
    if(NOT _target_location OR NOT EXISTS "${_target_location}")
      continue()
    endif()

    execute_process(
      COMMAND "${IBRT_OTOOL_EXECUTABLE}" -L "${_target_location}"
      OUTPUT_VARIABLE _otool_output
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    string(REGEX MATCHALL "\n[ \t]+/[^ \t\n]+\\.dylib" _absolute_deps "${_otool_output}")
    foreach(_dep IN LISTS _absolute_deps)
      string(STRIP "${_dep}" _dep)
      if(_dep MATCHES "^/(usr/lib|System/Library)/")
        continue()
      endif()
      get_filename_component(_dep_dir "${_dep}" DIRECTORY)
      list(APPEND _absolute_dirs "${_dep_dir}")
    endforeach()
  endforeach()

  if(_absolute_dirs)
    list(REMOVE_DUPLICATES _absolute_dirs)
  endif()

  set(${out_var} "${_absolute_dirs}" PARENT_SCOPE)
endfunction()

macro(ibrt_configure_dependencies)
  ibrt_require_directory(BEXT_INSTALL_DIR "the built bext install tree, typically <bext>/.build/install")
  ibrt_require_directory(BRLCAD_PREFIX "the BRL-CAD install prefix")

  if(EXISTS "${BEXT_INSTALL_DIR}/.build/install" AND EXISTS "${BEXT_INSTALL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "BEXT_INSTALL_DIR must point at the bext install tree, not the bext source checkout. "
      "Use something like <bext>/.build/install.")
  endif()

  if(NOT EXISTS "${BEXT_INSTALL_DIR}/lib/cmake")
    message(FATAL_ERROR
      "BEXT_INSTALL_DIR does not look like a bext install tree. "
      "Expected to find ${BEXT_INSTALL_DIR}/lib/cmake.")
  endif()

  list(PREPEND CMAKE_PREFIX_PATH "${BEXT_INSTALL_DIR}")

  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Test)
  find_package(OpenGL REQUIRED)
  find_package(embree REQUIRED CONFIG)
  find_package(rkcommon REQUIRED CONFIG)
  find_package(ispcrt REQUIRED CONFIG)
  if(WIN32)
    find_package(TBB REQUIRED COMPONENTS tbb CONFIG)
  endif()
  find_package(ospray REQUIRED CONFIG)

  if(NOT RKCOMMON_INCLUDE_DIRS)
    get_target_property(RKCOMMON_INCLUDE_DIRS rkcommon::rkcommon INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  if(NOT EMBREE_INCLUDE_DIRS)
    get_target_property(EMBREE_INCLUDE_DIRS embree INTERFACE_INCLUDE_DIRECTORIES)
  endif()

  if(EXISTS "${ispcrt_DIR}/ispc.cmake")
    list(PREPEND CMAKE_MODULE_PATH "${ispcrt_DIR}")
  else()
    message(FATAL_ERROR
      "Expected to find ispc.cmake beside ispcrtConfig.cmake under ${ispcrt_DIR}.")
  endif()

  # osprayUse.cmake expects an Open VKL target even when volumes are disabled.
  if(NOT OSPRAY_ENABLE_VOLUMES AND NOT TARGET openvkl::openvkl)
    add_library(openvkl::openvkl INTERFACE IMPORTED)
  endif()

  include("${ospray_DIR}/osprayUse.cmake")

  if(OSPRAY_ENABLE_VOLUMES AND TARGET openvkl::openvkl AND NOT OPENVKL_INCLUDE_DIRS)
    get_target_property(OPENVKL_INCLUDE_DIRS openvkl::openvkl INTERFACE_INCLUDE_DIRECTORIES)
  endif()

  find_path(IBRT_BRLCAD_INCLUDE_DIR
    NAMES brlcad/rt/db4.h
    PATHS "${BRLCAD_PREFIX}/include"
    NO_DEFAULT_PATH
    REQUIRED)
  find_library(IBRT_BRLCAD_RT_LIBRARY
    NAMES rt librt
    PATHS "${BRLCAD_PREFIX}/lib" "${BRLCAD_PREFIX}/lib64"
    NO_DEFAULT_PATH
    REQUIRED)
  find_library(IBRT_BRLCAD_BU_LIBRARY
    NAMES bu libbu
    PATHS "${BRLCAD_PREFIX}/lib" "${BRLCAD_PREFIX}/lib64"
    NO_DEFAULT_PATH
    REQUIRED)
  find_library(IBRT_BRLCAD_BN_LIBRARY
    NAMES bn libbn
    PATHS "${BRLCAD_PREFIX}/lib" "${BRLCAD_PREFIX}/lib64"
    NO_DEFAULT_PATH
    REQUIRED)

  set(IBRT_BRLCAD_INCLUDE_DIRS "${IBRT_BRLCAD_INCLUDE_DIR}")
  if(EXISTS "${IBRT_BRLCAD_INCLUDE_DIR}/brlcad/common.h")
    list(APPEND IBRT_BRLCAD_INCLUDE_DIRS "${IBRT_BRLCAD_INCLUDE_DIR}/brlcad")
  endif()
  if(EXISTS "${IBRT_BRLCAD_INCLUDE_DIR}/OpenNURBS/opennurbs.h")
    list(APPEND IBRT_BRLCAD_INCLUDE_DIRS "${IBRT_BRLCAD_INCLUDE_DIR}/OpenNURBS")
  endif()

  get_filename_component(IBRT_BRLCAD_LIBRARY_DIR "${IBRT_BRLCAD_RT_LIBRARY}" DIRECTORY)
  set(IBRT_BEXT_RUNTIME_DIR "${BEXT_INSTALL_DIR}/bin")
  set(IBRT_BEXT_LIBRARY_DIR "${BEXT_INSTALL_DIR}/lib")
  set(IBRT_BRLCAD_RUNTIME_DIR "${BRLCAD_PREFIX}/bin")

  ibrt_collect_apple_absolute_dependency_dirs(IBRT_EXTERNAL_RUNTIME_LIBRARY_DIRS)
  if(IBRT_EXTERNAL_RUNTIME_LIBRARY_DIRS)
    message(STATUS "IBRT external runtime dependency dirs: ${IBRT_EXTERNAL_RUNTIME_LIBRARY_DIRS}")
  endif()
endmacro()
