## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

add_library(ospray_module_brl_cad SHARED
  geometry/brlcad.cpp
  moduleInit.cpp
)

target_compile_definitions(ospray_module_brl_cad PRIVATE OBJECTFACTORY_IMPORT)

target_include_directories(ospray_module_brl_cad PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
  "${OSPRAY_INCLUDE_DIR}"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK/api"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK/common"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK/fb"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK/modules/cpu"
  "${RKCOMMON_INCLUDE_DIRS}"
  "${EMBREE_INCLUDE_DIRS}"
  ${IBRT_BRLCAD_INCLUDE_DIRS}
)

if(OSPRAY_ENABLE_VOLUMES)
  target_compile_definitions(ospray_module_brl_cad PRIVATE OSPRAY_ENABLE_VOLUMES)
  target_include_directories(ospray_module_brl_cad PRIVATE ${OPENVKL_INCLUDE_DIRS})
  add_definitions_ispc(-DOSPRAY_ENABLE_VOLUMES)
endif()

add_definitions_ispc(
  -DOSPRAY_BEGIN_ISPC_NAMESPACE=
  -DOSPRAY_END_ISPC_NAMESPACE=
  -DSYCL_EXTERNAL=
)

include_directories_ispc(
  "${OSPRAY_INCLUDE_DIR}"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK"
  "${OSPRAY_INCLUDE_DIR}/ospray/SDK/modules/cpu"
  "${RKCOMMON_INCLUDE_DIRS}"
  "${EMBREE_INCLUDE_DIRS}"
  ${IBRT_BRLCAD_INCLUDE_DIRS}
  "${CMAKE_CURRENT_SOURCE_DIR}/geometry"
)

if(OSPRAY_ENABLE_VOLUMES)
  include_directories_ispc(${OPENVKL_INCLUDE_DIRS})
endif()

ispc_target_add_sources(ospray_module_brl_cad geometry/brlcad.ispc)

target_link_libraries(ospray_module_brl_cad PRIVATE
  ospray::ospray
  ospray::ospray_module_cpu
  rkcommon::rkcommon
  ispcrt::ispcrt
  ${IBRT_BRLCAD_RT_LIBRARY}
  ${IBRT_BRLCAD_BU_LIBRARY}
  ${IBRT_BRLCAD_BN_LIBRARY}
)

if(APPLE)
  set(_ibrt_plugin_rpath
    "@loader_path"
    ${IBRT_EXTERNAL_RUNTIME_LIBRARY_DIRS}
    "${IBRT_BEXT_LIBRARY_DIR}"
    "${IBRT_BRLCAD_LIBRARY_DIR}")
elseif(UNIX)
  set(_ibrt_plugin_rpath
    "\$ORIGIN"
    ${IBRT_EXTERNAL_RUNTIME_LIBRARY_DIRS}
    "${IBRT_BEXT_LIBRARY_DIR}"
    "${IBRT_BRLCAD_LIBRARY_DIR}")
endif()
if(UNIX)
  list(REMOVE_DUPLICATES _ibrt_plugin_rpath)
endif()

set_target_properties(ospray_module_brl_cad PROPERTIES
  VERSION ${OSPRAY_VERSION}
  SOVERSION ${OSPRAY_SOVERSION}
)

if(UNIX)
  set_target_properties(ospray_module_brl_cad PROPERTIES
    BUILD_RPATH "${_ibrt_plugin_rpath}"
    INSTALL_RPATH "${_ibrt_plugin_rpath}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
  )
endif()

install(TARGETS ospray_module_brl_cad
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
