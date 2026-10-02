## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

foreach(_required IN ITEMS
    IBRT_WINDEPLOYQT_EXECUTABLE
    IBRT_TARGET_FILE
    IBRT_DEPLOY_LOCK)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

# IBRT and IBRTTests share an output directory.  Parallel windeployqt
# processes would update the same Qt DLL and plugin files concurrently.
file(LOCK "${IBRT_DEPLOY_LOCK}"
  GUARD PROCESS
  TIMEOUT 300
  RESULT_VARIABLE _lock_result)
if(NOT _lock_result STREQUAL "0")
  message(FATAL_ERROR
    "Could not acquire the windeployqt lock ${IBRT_DEPLOY_LOCK}: ${_lock_result}")
endif()

execute_process(
  COMMAND "${IBRT_WINDEPLOYQT_EXECUTABLE}" "${IBRT_TARGET_FILE}"
  RESULT_VARIABLE _deploy_result)
if(NOT _deploy_result EQUAL 0)
  message(FATAL_ERROR
    "windeployqt failed for ${IBRT_TARGET_FILE} with exit code ${_deploy_result}")
endif()
