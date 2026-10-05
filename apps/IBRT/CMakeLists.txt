## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

set(CMAKE_AUTOMOC ON)
# IBRT has no .ui or .qrc inputs.  Leaving these generators enabled makes
# CMake probe unused Qt build tools on every configure and needlessly expands
# the runtime toolchain required by headless-only builds.
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

set(_ibrt_render_core_sources
  hiddenlineeffect.cpp
  hiddenlineeffect.h
  hiddenlinerenderer.cpp
  hiddenlinerenderer.h
  ospraybackend.cpp
  ospraybackend.h
  renderappearance.h
  worker_ipc.cpp
  worker_ipc.h
)

set(_ibrt_ui_logic_sources
  qualitysettings.cpp
  qualitysettings.h
  renderworkflowlogic.cpp
  renderworkflowlogic.h
  renderworkerqueuelogic.cpp
  renderworkerqueuelogic.h
  renderreplaylogic.cpp
  renderreplaylogic.h
  interactioncontroller.cpp
  interactioncontroller.h
)

set(_ibrt_viewer_sources
  cellplotoverlaycontroller.cpp
  cellplotoverlaycontroller.h
  cellplotpluginloader.cpp
  cellplotpluginloader.h
  main.cpp
  mainwindow.cpp
  mainwindow.h
  renderworkerclient.cpp
  renderworkerclient.h
  renderwidget.cpp
  renderwidget.h
)

set(_ibrt_imgui_sources
  imgui/imgui.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_tables.cpp
  imgui/imgui_widgets.cpp
  imgui/backends/imgui_impl_opengl3.cpp
)

set(_ibrt_test_sources
  tests_ibrt.cpp
  renderworkerclient.cpp
  renderworkerclient.h
)

if(APPLE)
  set(_ibrt_origin "@loader_path")
elseif(UNIX)
  set(_ibrt_origin "\$ORIGIN")
endif()

if(UNIX)
  set(_ibrt_runtime_rpath "${_ibrt_origin};${IBRT_BEXT_LIBRARY_DIR};${IBRT_BRLCAD_LIBRARY_DIR}")
  if(APPLE)
    set(_ibrt_runtime_patterns
      "${IBRT_BEXT_LIBRARY_DIR}/libospray.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libospray.*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libospray_module_cpu*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/librkcommon*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libembree*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libtbb*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libispcrt*.dylib"
      "${IBRT_BEXT_LIBRARY_DIR}/libopenvkl*.dylib")
  else()
    set(_ibrt_runtime_patterns
      "${IBRT_BEXT_LIBRARY_DIR}/libospray.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libospray_module_cpu.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/librkcommon.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libembree.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libtbb.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libispcrt.so*"
      "${IBRT_BEXT_LIBRARY_DIR}/libopenvkl.so*")
  endif()

  file(GLOB _ibrt_local_ospray_runtime_libs
    LIST_DIRECTORIES FALSE
    ${_ibrt_runtime_patterns})
endif()

if(WIN32)
  set(_ibrt_windows_runtime_search_dirs
    "${IBRT_BEXT_RUNTIME_DIR}"
    "${IBRT_BRLCAD_RUNTIME_DIR}"
    "$<TARGET_FILE_DIR:Qt6::Core>")

  set(_ibrt_runtime_dependency_tool)
  set(_ibrt_runtime_dependency_command)
  if(CMAKE_OBJDUMP AND EXISTS "${CMAKE_OBJDUMP}")
    set(_ibrt_runtime_dependency_tool objdump)
    set(_ibrt_runtime_dependency_command "${CMAKE_OBJDUMP}")
  elseif(MSVC AND CMAKE_LINKER)
    get_filename_component(_ibrt_msvc_tool_dir "${CMAKE_LINKER}" DIRECTORY)
    find_program(_ibrt_dumpbin_executable
      NAMES dumpbin dumpbin.exe
      HINTS "${_ibrt_msvc_tool_dir}"
      NO_DEFAULT_PATH)
    if(_ibrt_dumpbin_executable)
      set(_ibrt_runtime_dependency_tool dumpbin)
      set(_ibrt_runtime_dependency_command "${_ibrt_dumpbin_executable}")
    endif()
  endif()
endif()

function(ibrt_configure_target target)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OSPRAY_INCLUDE_DIR}"
    "${RKCOMMON_INCLUDE_DIRS}"
    ${IBRT_BRLCAD_INCLUDE_DIRS}
  )
  target_link_libraries(${target} PRIVATE
    ospray::ospray
    ${IBRT_BRLCAD_RT_LIBRARY}
    ${IBRT_BRLCAD_BU_LIBRARY}
    ${IBRT_BRLCAD_BN_LIBRARY}
    ${IBRT_BRLCAD_BV_LIBRARY}
    $<$<BOOL:${WIN32}>:TBB::tbb>
  )

  if(UNIX)
    set_target_properties(${target} PROPERTIES
      BUILD_RPATH "${_ibrt_runtime_rpath}"
      INSTALL_RPATH "${_ibrt_runtime_rpath}"
      INSTALL_RPATH_USE_LINK_PATH TRUE
    )
  endif()
endfunction()

function(ibrt_deploy_brl_cad_module target)
  if(TARGET ospray_module_brl_cad)
    set(_deploy_target IBRT_deploy_brl_cad_module_runtime)
    if(NOT TARGET ${_deploy_target})
      if(UNIX)
        add_custom_target(${_deploy_target}
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ospray_module_brl_cad>"
            "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:ospray_module_brl_cad>"
          COMMAND ln -sfn
            "$<TARGET_FILE_NAME:ospray_module_brl_cad>"
            "$<TARGET_FILE_DIR:${target}>/$<TARGET_SONAME_FILE_NAME:ospray_module_brl_cad>"
          COMMAND ln -sfn
            "$<TARGET_SONAME_FILE_NAME:ospray_module_brl_cad>"
            "$<TARGET_FILE_DIR:${target}>/$<TARGET_LINKER_FILE_NAME:ospray_module_brl_cad>"
          DEPENDS ospray_module_brl_cad
          COMMENT "Deploying BRL-CAD OSPRay module runtime...")
      else()
        add_custom_target(${_deploy_target}
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ospray_module_brl_cad>"
            "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:ospray_module_brl_cad>"
          DEPENDS ospray_module_brl_cad
          COMMENT "Deploying BRL-CAD OSPRay module runtime...")
      endif()
    endif()
    add_dependencies(${target} ${_deploy_target})
  endif()
endfunction()

function(ibrt_deploy_worker companion)
  if(TARGET IBRTRenderWorker)
    add_dependencies(${companion} IBRTRenderWorker)
    add_custom_command(TARGET ${companion} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:IBRTRenderWorker>"
        "$<TARGET_FILE_DIR:${companion}>/$<TARGET_FILE_NAME:IBRTRenderWorker>"
      COMMENT "Deploying render worker for ${companion}...")
  endif()
endfunction()

function(ibrt_deploy_cell_plot_plugin target)
  if(TARGET ibrt_cell_plot_plugin)
    add_dependencies(${target} ibrt_cell_plot_plugin)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${target}>/plugins/visualizations"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ibrt_cell_plot_plugin>"
        "$<TARGET_FILE_DIR:${target}>/plugins/visualizations/$<TARGET_FILE_NAME:ibrt_cell_plot_plugin>"
      COMMENT "Deploying prototype cell-plot plugin for ${target}...")
  endif()
endfunction()

function(ibrt_deploy_demo_models target)
  set(_db_dir "${BRLCAD_PREFIX}/share/db")
  if(NOT EXISTS "${_db_dir}")
    return()
  endif()

  foreach(_demo IN ITEMS moss.g havoc.g axis.g)
    if(EXISTS "${_db_dir}/${_demo}")
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_db_dir}/${_demo}"
          "$<TARGET_FILE_DIR:${target}>/models/${_demo}"
        COMMENT "Deploying demo database ${_demo} for ${target}...")
    endif()
  endforeach()
endfunction()

function(ibrt_deploy_runtime_bundle target)
  if(WIN32)
    set(_runtime_modules)
    set(_runtime_search_dirs ${_ibrt_windows_runtime_search_dirs})
    foreach(_module_target IN LISTS ARGN)
      if(TARGET ${_module_target})
        add_dependencies(${target} ${_module_target})
        list(APPEND _runtime_modules "$<TARGET_FILE:${_module_target}>")
        list(APPEND _runtime_search_dirs "$<TARGET_FILE_DIR:${_module_target}>")
      endif()
    endforeach()

    list(REMOVE_DUPLICATES _runtime_search_dirs)
    list(JOIN _runtime_modules "@IBRT_LIST_SEPARATOR@" _encoded_runtime_modules)
    list(JOIN _runtime_search_dirs "@IBRT_LIST_SEPARATOR@" _encoded_runtime_search_dirs)

    set(_runtime_tool_arguments)
    if(_ibrt_runtime_dependency_tool)
      list(APPEND _runtime_tool_arguments
        "-DIBRT_RUNTIME_DEPENDENCY_TOOL=${_ibrt_runtime_dependency_tool}"
        "-DIBRT_RUNTIME_DEPENDENCY_COMMAND=${_ibrt_runtime_dependency_command}")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
        "-DIBRT_RUNTIME_EXECUTABLES=$<TARGET_FILE:${target}>"
        "-DIBRT_RUNTIME_MODULES=${_encoded_runtime_modules}"
        "-DIBRT_RUNTIME_SEARCH_DIRECTORIES=${_encoded_runtime_search_dirs}"
        "-DIBRT_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:${target}>"
        "-DIBRT_RUNTIME_DEPENDENCY_PLATFORM=windows+pe"
        ${_runtime_tool_arguments}
        -P "${PROJECT_SOURCE_DIR}/cmake/IBRTDeployRuntimeDependencies.cmake"
      COMMENT "Resolving runtime libraries for ${target}..."
      VERBATIM)
  elseif(UNIX)
    foreach(_lib IN LISTS _ibrt_local_ospray_runtime_libs)
      get_filename_component(_lib_name "${_lib}" NAME)
      if(IS_SYMLINK "${_lib}")
        file(READ_SYMLINK "${_lib}" _lib_link_target)
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:${target}>/${_lib_name}"
          COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${_lib_link_target}"
            "$<TARGET_FILE_DIR:${target}>/${_lib_name}"
          COMMENT "Creating local OSPRay runtime symlink ${_lib_name} for ${target}...")
      else()
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_lib}"
            "$<TARGET_FILE_DIR:${target}>/${_lib_name}"
          COMMENT "Copying local OSPRay runtime library ${_lib_name} for ${target}...")
      endif()
    endforeach()
  endif()
endfunction()

if(IBRT_ENABLE_RENDER_WORKER)
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(IBRTRenderWorker
      ${_ibrt_render_core_sources}
      worker_main.cpp
    )
    ibrt_configure_target(IBRTRenderWorker)
    ibrt_deploy_runtime_bundle(IBRTRenderWorker ospray_module_brl_cad)
    ibrt_deploy_brl_cad_module(IBRTRenderWorker)
  else()
    message(STATUS "IBRT render worker is supported only on Windows, macOS, and Linux; this build will use in-process rendering only.")
  endif()
else()
  message(STATUS "IBRT render worker target is disabled; the viewer will use local rendering only.")
endif()

if(IBRT_BUILD_VIEWER)
  qt_add_executable(IBRT
    ${_ibrt_render_core_sources}
    ${_ibrt_ui_logic_sources}
    ${_ibrt_viewer_sources}
    ${_ibrt_imgui_sources}
  )

  target_include_directories(IBRT PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
  )
  target_compile_definitions(IBRT PRIVATE BRLCAD_INSTALL_PREFIX="${BRLCAD_PREFIX}")
  target_compile_definitions(IBRT PRIVATE IBRT_VERSION="${PROJECT_VERSION}")
  target_link_libraries(IBRT PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    OpenGL::GL
    $<$<BOOL:${WIN32}>:dbghelp>
  )
  ibrt_configure_target(IBRT)
  ibrt_deploy_runtime_bundle(IBRT ospray_module_brl_cad ibrt_cell_plot_plugin)
  ibrt_deploy_brl_cad_module(IBRT)
  ibrt_deploy_worker(IBRT)
  ibrt_deploy_cell_plot_plugin(IBRT)
  ibrt_deploy_demo_models(IBRT)

  if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
      NAMES windeployqt windeployqt.exe
      HINTS "${BEXT_INSTALL_DIR}/bin")
    if(NOT WINDEPLOYQT_EXECUTABLE)
      message(FATAL_ERROR
        "windeployqt was not found under ${BEXT_INSTALL_DIR}/bin. "
        "Ensure the bext install includes Qt tools.")
    endif()

    add_custom_command(TARGET IBRT POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:IBRT>/vc_redist.x86.exe"
        "$<TARGET_FILE_DIR:IBRT>/vc_redist.x64.exe"
        "$<TARGET_FILE_DIR:IBRT>/vc_redist.arm64.exe"
      COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --no-compiler-runtime "$<TARGET_FILE:IBRT>"
      COMMENT "Running windeployqt for IBRT...")
  endif()

  install(TARGETS IBRT
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(TARGET IBRTRenderWorker)
  install(TARGETS IBRTRenderWorker
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

add_executable(IBRTReferenceRender
  ${_ibrt_render_core_sources}
  reference_render.cpp
)
target_link_libraries(IBRTReferenceRender PRIVATE
  Qt6::Core
  Qt6::Gui
)
ibrt_configure_target(IBRTReferenceRender)
ibrt_deploy_runtime_bundle(IBRTReferenceRender ospray_module_brl_cad)
ibrt_deploy_brl_cad_module(IBRTReferenceRender)

# General-purpose headless still renderer (CLI): model/object, renderer,
# projection, resolution, camera az/el, frames, background.
add_executable(IBRTOfflineRender
  ${_ibrt_render_core_sources}
  cellplotpluginloader.cpp
  cellplotpluginloader.h
  offline_render.cpp
)
target_include_directories(IBRTOfflineRender PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(IBRTOfflineRender PRIVATE
  Qt6::Core
  Qt6::Gui
)
ibrt_configure_target(IBRTOfflineRender)
ibrt_deploy_runtime_bundle(IBRTOfflineRender ospray_module_brl_cad ibrt_cell_plot_plugin)
ibrt_deploy_brl_cad_module(IBRTOfflineRender)
ibrt_deploy_cell_plot_plugin(IBRTOfflineRender)

if(IBRT_BUILD_TESTS AND BUILD_TESTING)
  add_executable(IBRTCompareReference compare_reference.cpp)
  target_link_libraries(IBRTCompareReference PRIVATE Qt6::Gui)
  ibrt_configure_target(IBRTCompareReference)
  ibrt_deploy_runtime_bundle(IBRTCompareReference)

  set(_ibrt_moss_db "${BRLCAD_PREFIX}/share/db/moss.g")
  if(EXISTS "${_ibrt_moss_db}")
    add_test(NAME IBRTReferenceMossSciVis
      COMMAND "${CMAKE_COMMAND}"
        "-DIBRT_RENDERER=$<TARGET_FILE:IBRTReferenceRender>"
        "-DIBRT_MOSS_DB=${_ibrt_moss_db}"
        "-DIBRT_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/moss-reference.png"
        "-DIBRT_REFERENCE=${PROJECT_SOURCE_DIR}/reference/2026-10-02-current-render/moss-all_scivis_1200x900.png"
        "-DIBRT_COMPARATOR=$<TARGET_FILE:IBRTCompareReference>"
        -P "${PROJECT_SOURCE_DIR}/cmake/IBRTVerifyReferenceRender.cmake")
    set_tests_properties(IBRTReferenceMossSciVis PROPERTIES TIMEOUT 300)

    add_test(NAME IBRTOfflineHiddenLineMoss
      COMMAND "$<TARGET_FILE:IBRTOfflineRender>"
        "${_ibrt_moss_db}"
        auto
        "${CMAKE_CURRENT_BINARY_DIR}/moss-hidden-lines.png"
        --renderer scivis
        --width 320
        --height 240
        --frames 1
        --pixel-samples 1
        --hidden-lines only)
    set_tests_properties(IBRTOfflineHiddenLineMoss PROPERTIES
      TIMEOUT 300
      LABELS integration)

    if(TARGET ibrt_cell_plot_plugin)
      add_test(NAME IBRTOfflineCellPlotMoss
        COMMAND "$<TARGET_FILE:IBRTOfflineRender>"
          "${_ibrt_moss_db}"
          auto
          "${CMAKE_CURRENT_BINARY_DIR}/moss-cell-plot.png"
          --renderer scivis
          --width 320
          --height 240
          --frames 1
          --pixel-samples 1
          --cell-plot
          --cell-plot-columns 32
          --cell-plot-rows 24)
      set_tests_properties(IBRTOfflineCellPlotMoss PROPERTIES
        TIMEOUT 300
        LABELS integration)
    endif()
  endif()
endif()

if(IBRT_BUILD_TESTS AND BUILD_TESTING)
  add_executable(IBRTTests
    ${_ibrt_render_core_sources}
    ${_ibrt_ui_logic_sources}
    ${_ibrt_test_sources}
  )

  target_compile_definitions(IBRTTests PRIVATE BRLCAD_INSTALL_PREFIX="${BRLCAD_PREFIX}")
  target_link_libraries(IBRTTests PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
  )
  ibrt_configure_target(IBRTTests)
  ibrt_deploy_runtime_bundle(IBRTTests ospray_module_brl_cad)
  ibrt_deploy_brl_cad_module(IBRTTests)
  ibrt_deploy_worker(IBRTTests)

  if(WIN32)
    if(NOT WINDEPLOYQT_EXECUTABLE)
      find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt windeployqt.exe
        HINTS "${BEXT_INSTALL_DIR}/bin")
    endif()

    add_custom_command(TARGET IBRTTests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:IBRTTests>/vc_redist.x86.exe"
        "$<TARGET_FILE_DIR:IBRTTests>/vc_redist.x64.exe"
        "$<TARGET_FILE_DIR:IBRTTests>/vc_redist.arm64.exe"
      COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --no-compiler-runtime "$<TARGET_FILE:IBRTTests>"
      COMMENT "Running windeployqt for IBRTTests...")
  endif()

  add_test(NAME IBRTTests COMMAND IBRTTests)

  # Fast, dependency-free unit tests for the standalone utility headers
  # (ibrt_constants.h, ipc_wire.h, colorramp.h, cameramath.h, imagecompare.h).
  # Plain C++ with no Qt/OSPRay/BRL-CAD linkage, so it builds and runs anywhere
  # and gives quick feedback without a full app/toolchain bring-up.
  add_executable(IBRTUnitTests
    hiddenlineeffect.cpp
    hiddenlineeffect.h
    worker_ipc.cpp
    worker_ipc.h
    tests_units.cpp)
  target_include_directories(IBRTUnitTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/include")
  target_compile_features(IBRTUnitTests PRIVATE cxx_std_20)
  add_test(NAME IBRTUnitTests COMMAND IBRTUnitTests)
  set_tests_properties(IBRTUnitTests PROPERTIES TIMEOUT 60 LABELS unit)
endif()
