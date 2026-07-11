## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

# this subdirectory builds the actual ospray plugin into a library
# "libospray_module_<modulename>.so".
#
# This shared library can depend on libospray, but should _not_ assume
# that it is living in the same address space as the application: When
# running in MPI mode ospray will, upon encountering the
# ospLoadModule("<moduleName>"), actually load and use this library on
# every worker node (which will typically not run the application). As
# such, the library should _not_ "commuincate" with the application
# through global variables or globally defined functions, and instead
# communicate solely through the ospNewGeometry(...), ospSet...(),
# ospCommit(...) etc functionality provided by ospray.


# build the actual shared library that defines the geometry.
#
# Note the name is important: In order for ospray to properly find and
# initialize a module referenced by a call to
# "ospLoadModule(<moduleName>) this module _has_ to
#
# a) be called libospray_module_<modulename>.so, and
# b) contain a (extern C linkage) initialization routine named
#    OSPError ospray_module_init_<moduleName>(int16_t versionMajor,
#        int16_t versionMinor, int16_t versionPatch)
#
add_library(ospray_module_brl_cad SHARED)
target_compile_features(ospray_module_brl_cad PRIVATE cxx_std_14)

target_compile_definitions(ospray_module_brl_cad PRIVATE OBJECTFACTORY_IMPORT)

set(BRLCAD_ROOT "" CACHE PATH "Path to BRL-CAD installation prefix")

find_path(BRLCAD_INCLUDE_DIR
  NAMES brlcad/common.h
  HINTS ${BRLCAD_ROOT} $ENV{BRLCAD_ROOT}
  PATH_SUFFIXES include
)

find_library(BRLCAD_LIBBU
  NAMES bu libbu
  HINTS ${BRLCAD_ROOT} $ENV{BRLCAD_ROOT}
  PATH_SUFFIXES lib lib64
)

find_library(BRLCAD_LIBBN
  NAMES bn libbn
  HINTS ${BRLCAD_ROOT} $ENV{BRLCAD_ROOT}
  PATH_SUFFIXES lib lib64
)

find_library(BRLCAD_LIBRT
  NAMES rt librt
  HINTS ${BRLCAD_ROOT} $ENV{BRLCAD_ROOT}
  PATH_SUFFIXES lib lib64
)

if (NOT BRLCAD_INCLUDE_DIR OR NOT BRLCAD_LIBBU OR NOT BRLCAD_LIBBN OR NOT BRLCAD_LIBRT)
  message(FATAL_ERROR
    "BRL-CAD development files not found. Set BRLCAD_ROOT to a BRL-CAD install prefix "
    "containing include/brlcad/common.h and libs libbu, libbn, librt.")
endif()

set(BRLCAD_INCLUDE_DIRS ${BRLCAD_INCLUDE_DIR})
if (EXISTS "${BRLCAD_INCLUDE_DIR}/brlcad/common.h")
  list(APPEND BRLCAD_INCLUDE_DIRS "${BRLCAD_INCLUDE_DIR}/brlcad")
endif()
if (EXISTS "${BRLCAD_INCLUDE_DIR}/OpenNURBS/opennurbs.h")
  list(APPEND BRLCAD_INCLUDE_DIRS "${BRLCAD_INCLUDE_DIR}/OpenNURBS")
endif()

target_include_directories(ospray_module_brl_cad BEFORE PRIVATE
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/ospray/include"
    ${RKCOMMON_INCLUDE_DIRS}
    ${EMBREE_INCLUDE_DIRS}
    ${BRLCAD_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
)

include_directories_ispc(${RKCOMMON_INCLUDE_DIRS} ${OSPRAY_CPU_MODULE_INCLUDE_DIRS})

# Required definitions for ISPC hybrid code for CPU
add_definitions_ispc(
  -DOSPRAY_BEGIN_ISPC_NAMESPACE=
  -DOSPRAY_END_ISPC_NAMESPACE=
  -DSYCL_EXTERNAL=
)

set(BRLCAD_MODULE_SOURCES
  # The C++ implementation of the BRL-CAD geometry plugin.
  geometry/brlcad.cpp
  # Module entry point required by ospLoadModule("brl_cad").
  moduleInit.cpp
)

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/geometry/brlcad.ispc")
  list(APPEND BRLCAD_MODULE_SOURCES geometry/brlcad.ispc)
endif()

ispc_target_add_sources(ospray_module_brl_cad ${BRLCAD_MODULE_SOURCES})

target_link_libraries(ospray_module_brl_cad
  PUBLIC ospray_module_cpu
  PRIVATE ${BRLCAD_LIBRT} ${BRLCAD_LIBBU} ${BRLCAD_LIBBN}
)

set_target_properties(ospray_module_brl_cad PROPERTIES
  VERSION ${OSPRAY_VERSION}
  SOVERSION ${OSPRAY_SOVERSION}
)
