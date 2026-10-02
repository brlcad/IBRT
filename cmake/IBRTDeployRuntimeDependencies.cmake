## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

foreach(_required_var IN ITEMS
    IBRT_RUNTIME_EXECUTABLES
    IBRT_RUNTIME_DESTINATION)
  if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
    message(FATAL_ERROR "${_required_var} is required")
  endif()
endforeach()

foreach(_list_var IN ITEMS
    IBRT_RUNTIME_EXECUTABLES
    IBRT_RUNTIME_MODULES
    IBRT_RUNTIME_SEARCH_DIRECTORIES)
  if(DEFINED ${_list_var})
    string(REPLACE "@IBRT_LIST_SEPARATOR@" ";"
      ${_list_var} "${${_list_var}}")
  endif()
endforeach()

if(DEFINED IBRT_RUNTIME_DEPENDENCY_PLATFORM
    AND NOT IBRT_RUNTIME_DEPENDENCY_PLATFORM STREQUAL "")
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM
    "${IBRT_RUNTIME_DEPENDENCY_PLATFORM}")
endif()
if(DEFINED IBRT_RUNTIME_DEPENDENCY_TOOL
    AND NOT IBRT_RUNTIME_DEPENDENCY_TOOL STREQUAL "")
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL
    "${IBRT_RUNTIME_DEPENDENCY_TOOL}")
endif()
if(DEFINED IBRT_RUNTIME_DEPENDENCY_COMMAND
    AND NOT IBRT_RUNTIME_DEPENDENCY_COMMAND STREQUAL "")
  set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND
    "${IBRT_RUNTIME_DEPENDENCY_COMMAND}")
endif()

foreach(_executable IN LISTS IBRT_RUNTIME_EXECUTABLES)
  if(NOT EXISTS "${_executable}")
    message(FATAL_ERROR "Runtime executable does not exist: ${_executable}")
  endif()
endforeach()
foreach(_module IN LISTS IBRT_RUNTIME_MODULES)
  if(NOT EXISTS "${_module}")
    message(FATAL_ERROR "Runtime module does not exist: ${_module}")
  endif()
endforeach()

set(_runtime_search_directories)
foreach(_directory IN LISTS IBRT_RUNTIME_SEARCH_DIRECTORIES)
  if(IS_DIRECTORY "${_directory}")
    list(APPEND _runtime_search_directories "${_directory}")
  endif()
endforeach()
if(WIN32)
  set(_environment_path "$ENV{PATH}")
  foreach(_directory IN LISTS _environment_path)
    if(IS_DIRECTORY "${_directory}")
      list(APPEND _runtime_search_directories "${_directory}")
    endif()
  endforeach()
endif()
list(REMOVE_DUPLICATES _runtime_search_directories)

set(_runtime_inputs EXECUTABLES ${IBRT_RUNTIME_EXECUTABLES})
if(IBRT_RUNTIME_MODULES)
  list(APPEND _runtime_inputs MODULES ${IBRT_RUNTIME_MODULES})
endif()

file(GET_RUNTIME_DEPENDENCIES
  RESOLVED_DEPENDENCIES_VAR _resolved_dependencies
  UNRESOLVED_DEPENDENCIES_VAR _unresolved_dependencies
  CONFLICTING_DEPENDENCIES_PREFIX _conflicting_dependencies
  ${_runtime_inputs}
  DIRECTORIES ${_runtime_search_directories}
  PRE_EXCLUDE_REGEXES
    "^[Aa][Pp][Ii]-[Mm][Ss]-[Ww][Ii][Nn]-"
    "^[Ee][Xx][Tt]-[Mm][Ss]-"
  POST_EXCLUDE_REGEXES
    "^[A-Za-z]:[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\]")

set(_missing_dependencies)
foreach(_dependency IN LISTS _unresolved_dependencies)
  string(TOLOWER "${_dependency}" _dependency_lower)
  if(NOT _dependency_lower MATCHES "^(api-ms-win-|ext-ms-win-)")
    list(APPEND _missing_dependencies "${_dependency}")
  endif()
endforeach()
if(_missing_dependencies)
  list(JOIN _missing_dependencies ", " _missing_text)
  message(FATAL_ERROR "Unresolved runtime dependencies: ${_missing_text}")
endif()

set(_incompatible_dependencies)
foreach(_dependency_name IN LISTS _conflicting_dependencies_FILENAMES)
  set(_conflict_var "_conflicting_dependencies_${_dependency_name}")
  set(_conflicting_paths ${${_conflict_var}})
  list(REMOVE_DUPLICATES _conflicting_paths)

  set(_files_match TRUE)
  unset(_reference_hash)
  foreach(_conflicting_path IN LISTS _conflicting_paths)
    file(SHA256 "${_conflicting_path}" _conflicting_hash)
    if(NOT DEFINED _reference_hash)
      set(_reference_hash "${_conflicting_hash}")
    elseif(NOT _conflicting_hash STREQUAL _reference_hash)
      set(_files_match FALSE)
      break()
    endif()
  endforeach()

  string(TOLOWER "${_dependency_name}" _dependency_name_lower)
  if(_files_match OR _dependency_name_lower MATCHES
      "^(concrt|msvcp|vcruntime)[0-9_]*[.]dll$")
    unset(_resolved_conflict)
    foreach(_search_directory IN LISTS _runtime_search_directories)
      cmake_path(ABSOLUTE_PATH _search_directory
        NORMALIZE OUTPUT_VARIABLE _search_directory)
      string(TOLOWER "${_search_directory}" _search_directory_lower)
      foreach(_conflicting_path IN LISTS _conflicting_paths)
        string(TOLOWER "${_conflicting_path}" _conflicting_path_lower)
        cmake_path(IS_PREFIX _search_directory_lower
          "${_conflicting_path_lower}" NORMALIZE _is_preferred_dependency)
        if(_is_preferred_dependency)
          set(_resolved_conflict "${_conflicting_path}")
          break()
        endif()
      endforeach()
      if(DEFINED _resolved_conflict)
        break()
      endif()
    endforeach()
    if(NOT DEFINED _resolved_conflict)
      list(GET _conflicting_paths 0 _resolved_conflict)
    endif()
    list(APPEND _resolved_dependencies "${_resolved_conflict}")
  else()
    list(APPEND _incompatible_dependencies "${_dependency_name}")
  endif()
endforeach()
if(_incompatible_dependencies)
  list(JOIN _incompatible_dependencies ", " _conflicting_text)
  message(FATAL_ERROR
    "Runtime dependencies resolve to incompatible files: ${_conflicting_text}")
endif()

file(MAKE_DIRECTORY "${IBRT_RUNTIME_DESTINATION}")
cmake_path(ABSOLUTE_PATH IBRT_RUNTIME_DESTINATION
  NORMALIZE OUTPUT_VARIABLE _destination)
string(TOLOWER "${_destination}" _destination_lower)

set(_windows_root "$ENV{WINDIR}")
if(NOT _windows_root STREQUAL "")
  cmake_path(ABSOLUTE_PATH _windows_root NORMALIZE OUTPUT_VARIABLE _windows_root)
  string(TOLOWER "${_windows_root}" _windows_root_lower)
endif()

set(_copied_names)
foreach(_dependency IN LISTS _resolved_dependencies)
  cmake_path(ABSOLUTE_PATH _dependency NORMALIZE OUTPUT_VARIABLE _dependency)
  string(TOLOWER "${_dependency}" _dependency_lower)

  if(NOT _windows_root STREQUAL "")
    cmake_path(IS_PREFIX _windows_root_lower "${_dependency_lower}"
      NORMALIZE _is_windows_system_dependency)
    if(_is_windows_system_dependency)
      continue()
    endif()
  endif()

  get_filename_component(_dependency_name "${_dependency}" NAME)
  set(_deployed_path "${_destination}/${_dependency_name}")
  string(TOLOWER "${_deployed_path}" _deployed_path_lower)
  if(_dependency_lower STREQUAL _deployed_path_lower)
    continue()
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_dependency}" "${_deployed_path}"
    RESULT_VARIABLE _copy_result)
  if(NOT _copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to deploy runtime dependency: ${_dependency}")
  endif()
  list(APPEND _copied_names "${_dependency_name}")
endforeach()

list(REMOVE_DUPLICATES _copied_names)
list(LENGTH _copied_names _copied_count)
message(STATUS "IBRT: deployed ${_copied_count} non-system runtime libraries")
