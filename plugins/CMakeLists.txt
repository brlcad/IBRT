## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

if(IBRT_BUILD_PLUGIN_BRL_CAD)
  add_subdirectory(brl_cad)
endif()

if(IBRT_BUILD_PLUGIN_CELL_PLOT)
  add_subdirectory(cell_plot)
endif()
