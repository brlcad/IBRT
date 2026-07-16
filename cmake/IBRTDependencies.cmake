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

# Return whether a Windows binary is a Debug build, by inspecting its import
# table with dumpbin. Sets <out_var> to TRUE, FALSE, or "UNKNOWN" (dumpbin
# unavailable or binary unreadable).
function(ibrt_binary_uses_debug_crt binary out_var)
  set(${out_var} "UNKNOWN" PARENT_SCOPE)
  if(NOT EXISTS "${binary}")
    return()
  endif()
  find_program(IBRT_DUMPBIN_EXECUTABLE NAMES dumpbin)
  if(NOT IBRT_DUMPBIN_EXECUTABLE)
    return()
  endif()

  file(TO_NATIVE_PATH "${binary}" _native_binary)
  execute_process(
    COMMAND "${IBRT_DUMPBIN_EXECUTABLE}" /dependents "${_native_binary}"
    OUTPUT_VARIABLE _deps_out
    ERROR_VARIABLE _deps_out
    RESULT_VARIABLE _deps_rc)
  if(NOT _deps_rc EQUAL 0)
    return()
  endif()

  # A Debug build pulls in the debug C runtime DLLs, which carry a trailing
  # 'D'/'d': MSVCP###D.dll, VCRUNTIME###D.dll, ucrtbased.dll. Compare
  # case-insensitively.
  string(TOUPPER "${_deps_out}" _deps_upper)
  if(_deps_upper MATCHES "MSVCP[0-9]+D\\.DLL"
      OR _deps_upper MATCHES "VCRUNTIME[0-9]+D\\.DLL"
      OR _deps_upper MATCHES "UCRTBASED\\.DLL")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Guard against pairing a Debug IBRT build with Release BRL-CAD/bext (or vice
# versa) on Windows, where Debug and Release builds cannot be mixed.
function(ibrt_check_dependency_runtime)
  if(NOT MSVC)
    return()
  endif()

  # Representative dependency binaries. bext is the tree most often
  # mismatched (a single Release bext used for both configs), so check it
  # first; the BRL-CAD runtime is a best-effort second probe.
  set(_probes "${BEXT_INSTALL_DIR}/bin/ospray.dll;bext (BEXT_INSTALL_DIR)")
  foreach(_brlcad_dll rt bu bn)
    foreach(_candidate
        "${BRLCAD_PREFIX}/bin/lib${_brlcad_dll}.dll"
        "${BRLCAD_PREFIX}/bin/${_brlcad_dll}.dll")
      if(EXISTS "${_candidate}")
        list(APPEND _probes "${_candidate};BRL-CAD (BRLCAD_PREFIX)")
        break()
      endif()
    endforeach()
  endforeach()

  if(IBRT_CONFIG_IS_DEBUG)
    set(_needs "Debug")
    set(_found "Release")
  else()
    set(_needs "Release")
    set(_found "Debug")
  endif()

  set(_checked FALSE)
  while(_probes)
    list(POP_FRONT _probes _binary _label)
    ibrt_binary_uses_debug_crt("${_binary}" _is_debug)
    if(_is_debug STREQUAL "UNKNOWN")
      continue()
    endif()
    set(_checked TRUE)
    if(NOT (_is_debug STREQUAL "${IBRT_CONFIG_IS_DEBUG}"))
      message(FATAL_ERROR
        "Debug/Release mismatch: this ${CMAKE_BUILD_TYPE} IBRT build needs "
        "${_needs} dependencies, but the ${_label} dependency at\n"
        "    ${_binary}\n"
        "is a ${_found} build. Debug and Release builds cannot be mixed. Point "
        "this build at a ${_needs} BRL-CAD and bext tree, or configure a "
        "matching IBRT build.")
    endif()
  endwhile()

  if(_checked)
    message(STATUS "IBRT: dependencies match this ${CMAKE_BUILD_TYPE} build.")
  else()
    message(STATUS
      "IBRT: could not verify the dependency build type (dumpbin unavailable "
      "or dependency DLLs not found); skipping the Debug/Release check.")
  endif()
endfunction()

macro(ibrt_configure_dependencies)
  # BEXT_INSTALL_DIR points at the bext install subdirectory. As an
  # alternative, BRLCAD_EXT_DIR may point at the bext build directory (which
  # holds install/ and noinstall/); in that case BEXT_INSTALL_DIR is
  # BRLCAD_EXT_DIR/install.
  if((NOT DEFINED BEXT_INSTALL_DIR OR BEXT_INSTALL_DIR STREQUAL "")
      AND DEFINED BRLCAD_EXT_DIR AND NOT BRLCAD_EXT_DIR STREQUAL "")
    set(BEXT_INSTALL_DIR "${BRLCAD_EXT_DIR}/install")
    message(STATUS "IBRT: using BEXT_INSTALL_DIR from BRLCAD_EXT_DIR: ${BEXT_INSTALL_DIR}")
  endif()

  ibrt_require_directory(BEXT_INSTALL_DIR "the built bext install tree, e.g. <bext-build>/install; or set BRLCAD_EXT_DIR to <bext-build>")
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

  # Fail early and clearly on a Debug/Release dependency mismatch (Windows).
  ibrt_check_dependency_runtime()

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
  find_library(IBRT_BRLCAD_BV_LIBRARY
    NAMES bv libbv
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
