## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

foreach(_required IN ITEMS IBRT_RENDERER IBRT_MOSS_DB IBRT_OUTPUT IBRT_REFERENCE)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

get_filename_component(_output_dir "${IBRT_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

execute_process(
  COMMAND "${IBRT_RENDERER}" "${IBRT_MOSS_DB}" all.g "${IBRT_OUTPUT}"
  RESULT_VARIABLE _render_result
  OUTPUT_VARIABLE _render_stdout
  ERROR_VARIABLE _render_stderr)
if(NOT _render_result EQUAL 0)
  message(FATAL_ERROR
    "Reference render failed (${_render_result})\n${_render_stdout}\n${_render_stderr}")
endif()

file(SHA256 "${IBRT_REFERENCE}" _expected_sha256)
file(SHA256 "${IBRT_OUTPUT}" _actual_sha256)
if(NOT _actual_sha256 STREQUAL _expected_sha256)
  message(FATAL_ERROR
    "Moss render differs from the known-good reference.\n"
    "Expected: ${_expected_sha256}\n"
    "Actual:   ${_actual_sha256}\n"
    "Output:   ${IBRT_OUTPUT}")
endif()

message(STATUS "Moss reference render matches ${_expected_sha256}")
