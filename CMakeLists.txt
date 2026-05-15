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

set(IBRT_RENDER_WORKER_DEFAULT OFF)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(IBRT_RENDER_WORKER_DEFAULT ON)
endif()

option(IBRT_BUILD_VIEWER "Build the IBRT desktop viewer" ON)
option(IBRT_BUILD_PLUGIN_BRL_CAD "Build the BRL-CAD OSPRay plugin" ON)
option(IBRT_BUILD_TESTS "Build the IBRT test suite" ${BUILD_TESTING})
option(IBRT_ENABLE_RENDER_WORKER "Build the out-of-process render worker" ${IBRT_RENDER_WORKER_DEFAULT})

set(BEXT_INSTALL_DIR "" CACHE PATH
  "Path to the bext install tree that provides OSPRay, Qt, and related dependencies")
set(BRLCAD_PREFIX "" CACHE PATH "Path to the BRL-CAD install prefix")

include(IBRTDependencies)
ibrt_configure_dependencies()

add_subdirectory(plugins)
add_subdirectory(apps)
