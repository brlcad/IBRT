if(NOT DEFINED IBRT_RUNTIME_DIR OR NOT IS_DIRECTORY "${IBRT_RUNTIME_DIR}")
  return()
endif()

file(GLOB _ibrt_tbb_runtime_libs
  LIST_DIRECTORIES FALSE
  "${IBRT_RUNTIME_DIR}/libtbb*.dylib")

if(_ibrt_tbb_runtime_libs)
  file(REMOVE ${_ibrt_tbb_runtime_libs})
endif()
