## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT
##
## Build-type handling for IBRT.
##
## A Debug or Release IBRT build must be paired with a BRL-CAD and bext of
## the same configuration; the two cannot be mixed within one build. IBRT
## therefore uses a single configuration per build directory, which also
## lets Debug and Release builds coexist on disk (see CMakePresets.json).

include_guard(GLOBAL)

# The MSVC runtime abstraction (CMAKE_MSVC_RUNTIME_LIBRARY) requires policy
# CMP0091 to be NEW. It defaults to NEW at our minimum CMake version; set it
# explicitly so an embedding project cannot flip it.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

function(ibrt_setup_build_types)
  get_property(_ibrt_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

  # Normalize the capitalization of a user-supplied build type so the
  # configuration comparisons below behave predictably.
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _ibrt_bt_upper)
    if(_ibrt_bt_upper STREQUAL "RELEASE")
      set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    elseif(_ibrt_bt_upper STREQUAL "DEBUG")
      set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    elseif(_ibrt_bt_upper STREQUAL "RELWITHDEBINFO")
      set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
    elseif(_ibrt_bt_upper STREQUAL "MINSIZEREL")
      set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "Build type" FORCE)
    endif()
  endif()

  # Single-config generators need a build type; default to Release.
  if(NOT _ibrt_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    message(STATUS "IBRT: no CMAKE_BUILD_TYPE set; defaulting to Release.")
  endif()

  # Multi-config generators would otherwise offer every configuration in one
  # build tree, but the BRL-CAD and bext trees are resolved once at configure
  # time, so a build tree can only match a single configuration. Collapse to
  # that one configuration and get Debug/Release coexistence from separate
  # build directories instead.
  if(_ibrt_multi_config)
    if(NOT CMAKE_BUILD_TYPE)
      set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
    set(CMAKE_CONFIGURATION_TYPES "${CMAKE_BUILD_TYPE}"
      CACHE STRING "IBRT uses one configuration per build tree" FORCE)
    message(STATUS
      "IBRT: multi-config generator detected; using the single configuration "
      "'${CMAKE_BUILD_TYPE}'. Configure a second build directory for the other "
      "configuration.")
  endif()

  # Keep the MSVC runtime library tied to the configuration (the compiler
  # default) so IBRT matches its dependencies. Only set it if the user has
  # not chosen otherwise.
  if(MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
      CACHE STRING "MSVC runtime library (follows the build configuration)")
  endif()

  # Record whether this is a Debug configuration, for the dependency guard.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IBRT_CONFIG_IS_DEBUG TRUE CACHE INTERNAL "IBRT is a Debug build")
  else()
    set(IBRT_CONFIG_IS_DEBUG FALSE CACHE INTERNAL "IBRT is a Debug build")
  endif()
endfunction()
