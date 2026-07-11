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

  # Out-of-tree ISPC callbacks must use the same SIMD ABI as OSPRay. The
  # installed ispcrt helper does not populate its ARM target list itself.
  if(NOT OSPRAY_ISPC_TARGET_LIST)
    message(FATAL_ERROR
      "The OSPRay package did not report OSPRAY_ISPC_TARGET_LIST; "
      "IBRT cannot safely compile external OSPRay ISPC modules.")
  endif()
  set(ISPC_TARGET_CPU "${OSPRAY_ISPC_TARGET_LIST}")
  message(STATUS "Building IBRT OSPRay plugins for ISPC targets: ${ISPC_TARGET_CPU}")

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
endmacro()
