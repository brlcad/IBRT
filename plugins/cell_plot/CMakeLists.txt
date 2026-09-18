## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

set(CMAKE_AUTOMOC ON)

add_library(ibrt_cell_plot_plugin MODULE
  brlcadcellplotplugin.cpp
  brlcadcellplotplugin.h
)

target_compile_features(ibrt_cell_plot_plugin PRIVATE cxx_std_17)
target_include_directories(ibrt_cell_plot_plugin PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  ${IBRT_BRLCAD_INCLUDE_DIRS}
)
target_link_libraries(ibrt_cell_plot_plugin PRIVATE
  Qt6::Core
  Qt6::Gui
  ${IBRT_BRLCAD_RT_LIBRARY}
  ${IBRT_BRLCAD_BU_LIBRARY}
  ${IBRT_BRLCAD_BN_LIBRARY}
)

set_target_properties(ibrt_cell_plot_plugin PROPERTIES
  OUTPUT_NAME "ibrt_cell_plot"
)

if(APPLE)
  set(_ibrt_cell_plot_rpath "@loader_path/../..;${IBRT_BRLCAD_LIBRARY_DIR}")
elseif(UNIX)
  set(_ibrt_cell_plot_rpath "\$ORIGIN/../..;${IBRT_BRLCAD_LIBRARY_DIR}")
endif()

if(UNIX)
  set_target_properties(ibrt_cell_plot_plugin PROPERTIES
    BUILD_RPATH "${_ibrt_cell_plot_rpath}"
    INSTALL_RPATH "${_ibrt_cell_plot_rpath}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
  )
endif()

install(TARGETS ibrt_cell_plot_plugin
  LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/visualizations"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/visualizations")

if(IBRT_BUILD_TESTS AND BUILD_TESTING)
  add_executable(IBRTCellPlotPluginSmoke cellplot_plugin_smoke.cpp)
  target_compile_features(IBRTCellPlotPluginSmoke PRIVATE cxx_std_17)
  target_include_directories(IBRTCellPlotPluginSmoke PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(IBRTCellPlotPluginSmoke PRIVATE Qt6::Core Qt6::Gui)

  set(_ibrt_cell_plot_moss_db "${BRLCAD_PREFIX}/share/db/moss.g")
  if(EXISTS "${_ibrt_cell_plot_moss_db}")
    add_test(NAME IBRTCellPlotPluginSmoke
      COMMAND IBRTCellPlotPluginSmoke
        "$<TARGET_FILE:ibrt_cell_plot_plugin>"
        "${_ibrt_cell_plot_moss_db}"
        all.g)
    set_tests_properties(IBRTCellPlotPluginSmoke PROPERTIES
      TIMEOUT 120
      LABELS "plugin;integration")
  endif()
endif()
