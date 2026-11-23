## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

project(IBRT VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_MESSAGE LAZY)

include(GNUInstallDirs)
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Normalize the build type and MSVC runtime so Debug/Release IBRT builds
# stay ABI-compatible with the matching BRL-CAD/bext trees. Must run after
# project() (needs MSVC / GENERATOR_IS_MULTI_CONFIG) and before any targets.
include(IBRTBuildTypes)
ibrt_setup_build_types()

set(IBRT_RENDER_WORKER_DEFAULT OFF)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(IBRT_RENDER_WORKER_DEFAULT ON)
endif()

option(IBRT_BUILD_VIEWER "Build the IBRT desktop viewer" ON)
option(IBRT_BUILD_PLUGIN_BRL_CAD "Build the BRL-CAD OSPRay plugin" ON)
option(IBRT_BUILD_PLUGIN_CELL_PLOT "Build the prototype cell-plot visualization plugin" ON)
option(IBRT_BUILD_TESTS "Build the IBRT test suite" ${BUILD_TESTING})
option(IBRT_ENABLE_RENDER_WORKER "Build the out-of-process render worker" ${IBRT_RENDER_WORKER_DEFAULT})

set(BEXT_INSTALL_DIR "" CACHE PATH
  "Path to the bext install tree that provides OSPRay, Qt, and related dependencies")
set(BRLCAD_EXT_DIR "" CACHE PATH
  "Path to the bext build directory (contains install/ and noinstall/); an alternative to BEXT_INSTALL_DIR, which is then BRLCAD_EXT_DIR/install")
set(IBRT_BUNDLE_MANIFEST "${CMAKE_BINARY_DIR}/ibrt-install-manifest.json" CACHE FILEPATH
  "JSON bundle manifest written by cmake --install; empty disables generation")

include(IBRTDependencies)
ibrt_configure_dependencies()
include(IBRTRuntimeDeployment)

add_subdirectory(plugins)
add_subdirectory(apps)

if(IBRT_BUNDLE_MANIFEST)
  set(IBRT_BUNDLE_INSTALL_PREFIXES
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}"
    "${BEXT_INSTALL_DIR}"
    "${BRLCAD_LIB_DIR}"
  )
  list(FILTER IBRT_BUNDLE_INSTALL_PREFIXES EXCLUDE REGEX "^$")
  list(TRANSFORM IBRT_BUNDLE_INSTALL_PREFIXES REPLACE "\\\\" "/")
  list(REMOVE_DUPLICATES IBRT_BUNDLE_INSTALL_PREFIXES)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IBRTInstallBundleManifest.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/IBRTInstallBundleManifest.cmake"
    @ONLY
  )
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/IBRTInstallBundleManifest.cmake")
endif()

if(IBRT_BUILD_TESTS AND BUILD_TESTING)
  add_test(NAME IBRTBundleManifestWriter
    COMMAND "${CMAKE_COMMAND}"
      "-DIBRT_MANIFEST_WRITER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/IBRTWriteBundleManifest.cmake"
      "-DIBRT_MANIFEST_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/manifest-writer-test"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IBRTWriteBundleManifestTest.cmake")
  set_tests_properties(IBRTBundleManifestWriter PROPERTIES LABELS "unit;cmake")
endif()
