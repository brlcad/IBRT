## Copyright (c) 2026 BRL-CAD Visualizer contributors
## SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(IBRT VERSION 0.1 LANGUAGES CXX)
set(IBRT_RENDER_WORKER_DEFAULT OFF)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(IBRT_RENDER_WORKER_DEFAULT ON)
endif()
option(IBRT_ENABLE_RENDER_WORKER "Build the out-of-process render worker" ${IBRT_RENDER_WORKER_DEFAULT})

set(IBRT_BUILD_RENDER_WORKER ${IBRT_ENABLE_RENDER_WORKER})
if(IBRT_ENABLE_RENDER_WORKER AND NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    message(STATUS "IBRT render worker is supported only on Windows, macOS, and Linux; this build will use in-process rendering only.")
    set(IBRT_BUILD_RENDER_WORKER OFF)
elseif(NOT IBRT_ENABLE_RENDER_WORKER)
    message(STATUS "IBRT render worker target is disabled; the viewer will use local rendering only.")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt: add its prefix to CMAKE_PREFIX_PATH, e.g.:
#   cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64 ...
# or set Qt6_DIR directly.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Test)
find_package(OpenGL REQUIRED)
include(CTest)

# ---- OSPRay / rkcommon / Embree / OpenVKL ----
# Pass via -D flags at configure time, e.g.:
#   -DOSPRAY_PREFIX=... -DRKCOMMON_PREFIX=... -DEMBREE_PREFIX=... -DOPENVKL_PREFIX=...
set(OSPRAY_PREFIX   "" CACHE PATH "OSPRay prefix")
set(RKCOMMON_PREFIX "" CACHE PATH "rkcommon prefix")

foreach(_pkg OSPRAY_PREFIX RKCOMMON_PREFIX)
    if(NOT ${_pkg})
        message(FATAL_ERROR "${_pkg} is not set. Pass it with -D${_pkg}=<path>")
    endif()
endforeach()

list(APPEND CMAKE_PREFIX_PATH "${OSPRAY_PREFIX}" "${RKCOMMON_PREFIX}")
find_package(ospray REQUIRED CONFIG)

if(WIN32)
    set(EMBREE_PREFIX  "" CACHE PATH "Embree prefix")
    set(OPENVKL_PREFIX "" CACHE PATH "OpenVKL prefix")
    set(TBB_ROOT       "" CACHE PATH "TBB root")

    foreach(_pkg EMBREE_PREFIX OPENVKL_PREFIX TBB_ROOT)
        if(NOT ${_pkg})
            message(FATAL_ERROR "${_pkg} is not set. Pass it with -D${_pkg}=<path>")
        endif()
    endforeach()

    find_package(TBB REQUIRED COMPONENTS tbb CONFIG
        HINTS "${TBB_ROOT}/lib/cmake/TBB" "${TBB_ROOT}/lib/cmake")
endif()

# ---- BRL-CAD ----
set(BRLCAD_PREFIX "" CACHE PATH "BRL-CAD prefix")
if(NOT BRLCAD_PREFIX)
    message(FATAL_ERROR "BRLCAD_PREFIX is not set. Pass it with -DBRLCAD_PREFIX=<path>")
endif()

# Optional explicit BRL-CAD OSPRay module deployment inputs.
set(BRLCAD_OSPRAY_MODULE_DLL "" CACHE FILEPATH
    "Path to ospray_module_brl_cad.dll (optional; auto-detected from known bin dirs if unset)")
set(BRLCAD_OSPRAY_MODULE_DEPLOY_DIR "" CACHE PATH
    "Optional directory with extra DLL dependencies for ospray_module_brl_cad.dll")

if(WIN32)
    set(_brlcad_module_auto_candidates
        "${CMAKE_SOURCE_DIR}/../../bcs/Debug/ospray_module_brl_cad.dll"
        "${CMAKE_SOURCE_DIR}/../../bcs/Release/ospray_module_brl_cad.dll"
        "${OSPRAY_PREFIX}/bin/ospray_module_brl_cad.dll"
        "${BRLCAD_PREFIX}/bin/ospray_module_brl_cad.dll")

    set(_should_refresh_brlcad_module_path OFF)
    if(NOT BRLCAD_OSPRAY_MODULE_DLL OR NOT EXISTS "${BRLCAD_OSPRAY_MODULE_DLL}")
        set(_should_refresh_brlcad_module_path ON)
    else()
        get_filename_component(_brlcad_module_name "${BRLCAD_OSPRAY_MODULE_DLL}" NAME)
        if(_brlcad_module_name STREQUAL "ospray_module_brl_cad.dll")
            foreach(_preferred_candidate IN LISTS _brlcad_module_auto_candidates)
                if(EXISTS "${_preferred_candidate}" AND NOT BRLCAD_OSPRAY_MODULE_DLL STREQUAL "${_preferred_candidate}")
                    set(_should_refresh_brlcad_module_path ON)
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(_should_refresh_brlcad_module_path)
        foreach(_candidate IN LISTS _brlcad_module_auto_candidates)
            if(EXISTS "${_candidate}")
                set(BRLCAD_OSPRAY_MODULE_DLL "${_candidate}" CACHE FILEPATH
                    "Path to ospray_module_brl_cad.dll (optional; auto-detected from known bin dirs if unset)"
                    FORCE)
                break()
            endif()
        endforeach()
    endif()
endif()

find_path(BRLCAD_INCLUDE_DIR brlcad/rt/db4.h PATHS "${BRLCAD_PREFIX}/include" REQUIRED)
find_library(BRLCAD_RT_LIBRARY NAMES rt librt PATHS "${BRLCAD_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(BRLCAD_BU_LIBRARY NAMES bu libbu PATHS "${BRLCAD_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)

qt_add_executable(QtOsprayViewer
    main.cpp
    mainwindow.cpp
    mainwindow.h
    renderworkerclient.cpp
    renderworkerclient.h
    renderwidget.cpp
    renderwidget.h
    qualitysettings.cpp
    qualitysettings.h
    renderworkflowlogic.cpp
    renderworkflowlogic.h
    renderworkerqueuelogic.cpp
    renderworkerqueuelogic.h
    renderreplaylogic.cpp
    renderreplaylogic.h
    ospraybackend.cpp
    ospraybackend.h
    interactioncontroller.cpp
    interactioncontroller.h
    worker_ipc.cpp
    worker_ipc.h
)

if(IBRT_BUILD_RENDER_WORKER)
    add_executable(IBRTRenderWorker
        worker_main.cpp
        ospraybackend.cpp
        ospraybackend.h
        worker_ipc.cpp
        worker_ipc.h
    )
endif()

add_executable(IBRTTests
    tests_ibrt.cpp
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
    ospraybackend.cpp
    ospraybackend.h
    renderworkerclient.cpp
    renderworkerclient.h
    worker_ipc.cpp
    worker_ipc.h
)

add_executable(IBRTReferenceRender
    reference_render.cpp
    ospraybackend.cpp
    ospraybackend.h
)

set_target_properties(QtOsprayViewer PROPERTIES
    OUTPUT_NAME "IBRT"
)

target_sources(QtOsprayViewer PRIVATE
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(QtOsprayViewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
    "${OSPRAY_PREFIX}/include"
    "${RKCOMMON_PREFIX}/include"
    "${BRLCAD_INCLUDE_DIR}"
    "${BRLCAD_INCLUDE_DIR}/brlcad"
    "${BRLCAD_INCLUDE_DIR}/OpenNURBS"
    $<$<BOOL:${WIN32}>:${TBB_ROOT}/include>
)

if(TARGET IBRTRenderWorker)
    target_include_directories(IBRTRenderWorker PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${OSPRAY_PREFIX}/include"
        "${RKCOMMON_PREFIX}/include"
        "${BRLCAD_INCLUDE_DIR}"
        "${BRLCAD_INCLUDE_DIR}/brlcad"
        "${BRLCAD_INCLUDE_DIR}/OpenNURBS"
        $<$<BOOL:${WIN32}>:${TBB_ROOT}/include>
    )
endif()

target_include_directories(IBRTTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OSPRAY_PREFIX}/include"
    "${RKCOMMON_PREFIX}/include"
    "${BRLCAD_INCLUDE_DIR}"
    "${BRLCAD_INCLUDE_DIR}/brlcad"
    "${BRLCAD_INCLUDE_DIR}/OpenNURBS"
    $<$<BOOL:${WIN32}>:${TBB_ROOT}/include>
)

target_include_directories(IBRTReferenceRender PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OSPRAY_PREFIX}/include"
    "${RKCOMMON_PREFIX}/include"
    "${BRLCAD_INCLUDE_DIR}"
    "${BRLCAD_INCLUDE_DIR}/brlcad"
    "${BRLCAD_INCLUDE_DIR}/OpenNURBS"
)

target_compile_definitions(QtOsprayViewer PRIVATE
    BRLCAD_INSTALL_PREFIX="${BRLCAD_PREFIX}"
)

target_compile_definitions(IBRTTests PRIVATE
    BRLCAD_INSTALL_PREFIX="${BRLCAD_PREFIX}"
)

target_link_libraries(QtOsprayViewer PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    ospray::ospray
    OpenGL::GL
    ${BRLCAD_RT_LIBRARY}
    ${BRLCAD_BU_LIBRARY}
    $<$<BOOL:${WIN32}>:TBB::tbb>
    $<$<BOOL:${WIN32}>:dbghelp>
)

if(TARGET IBRTRenderWorker)
    target_link_libraries(IBRTRenderWorker PRIVATE
        ospray::ospray
        ${BRLCAD_RT_LIBRARY}
        ${BRLCAD_BU_LIBRARY}
        $<$<BOOL:${WIN32}>:TBB::tbb>
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(IBRTRenderWorker PRIVATE Qt6::Core)
    endif()
endif()

target_link_libraries(IBRTTests PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    ospray::ospray
    ${BRLCAD_RT_LIBRARY}
    ${BRLCAD_BU_LIBRARY}
    $<$<BOOL:${WIN32}>:TBB::tbb>
)

target_link_libraries(IBRTReferenceRender PRIVATE
    Qt6::Core
    Qt6::Gui
    ospray::ospray
    ${BRLCAD_RT_LIBRARY}
    ${BRLCAD_BU_LIBRARY}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_target_properties(QtOsprayViewer PROPERTIES
        INSTALL_RPATH "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
        BUILD_RPATH   "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
    if(TARGET IBRTRenderWorker)
        set_target_properties(IBRTRenderWorker PROPERTIES
            INSTALL_RPATH "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
            BUILD_RPATH   "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )
    endif()
    set_target_properties(IBRTTests PROPERTIES
        INSTALL_RPATH "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
        BUILD_RPATH   "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
    set_target_properties(IBRTReferenceRender PROPERTIES
        INSTALL_RPATH "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
        BUILD_RPATH   "${OSPRAY_PREFIX}/lib;${RKCOMMON_PREFIX}/lib;${BRLCAD_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
endif()

if(WIN32 AND TARGET IBRTRenderWorker)
    set(TBB_REDIST_DIR "${TBB_ROOT}/redist/intel64/vc14")

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OSPRAY_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>"
        COMMENT "Copying OSPRay runtime DLLs for worker...")

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RKCOMMON_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>"
        COMMENT "Copying rkcommon runtime DLLs for worker...")

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EMBREE_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>"
        COMMENT "Copying Embree runtime DLLs for worker...")

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENVKL_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>"
        COMMENT "Copying OpenVKL runtime DLLs for worker...")

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BRLCAD_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>"
        COMMENT "Copying BRL-CAD runtime DLLs for worker...")

    if(EXISTS "${TBB_REDIST_DIR}")
        add_custom_command(TARGET IBRTRenderWorker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TBB_REDIST_DIR}"
                "$<TARGET_FILE_DIR:IBRTRenderWorker>"
            COMMENT "Copying TBB runtime DLLs for worker...")
    endif()

    if(BRLCAD_OSPRAY_MODULE_DLL AND EXISTS "${BRLCAD_OSPRAY_MODULE_DLL}")
        add_custom_command(TARGET IBRTRenderWorker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BRLCAD_OSPRAY_MODULE_DLL}"
                "$<TARGET_FILE_DIR:IBRTRenderWorker>/ospray_module_brl_cad.dll"
            COMMENT "Deploying ospray_module_brl_cad.dll for worker...")
    endif()

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${RKCOMMON_PREFIX}/bin/rkcommon.dll"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>/rkcommon.dll"
        COMMENT "Restoring correct rkcommon.dll for worker...")
    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${EMBREE_PREFIX}/bin/embree4.dll"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>/embree4.dll"
        COMMENT "Restoring correct embree4.dll for worker...")

    add_custom_command(TARGET IBRTRenderWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:IBRTRenderWorker>/vcruntime140.dll"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>/vcruntime140_1.dll"
            "$<TARGET_FILE_DIR:IBRTRenderWorker>/msvcp140.dll"
        COMMENT "Removing bundled CRT DLLs for worker...")

    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OSPRAY_PREFIX}/bin"
            "$<TARGET_FILE_DIR:QtOsprayViewer>"
        COMMENT "Copying OSPRay runtime DLLs...")

    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RKCOMMON_PREFIX}/bin"
            "$<TARGET_FILE_DIR:QtOsprayViewer>"
        COMMENT "Copying rkcommon runtime DLLs...")

    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EMBREE_PREFIX}/bin"
            "$<TARGET_FILE_DIR:QtOsprayViewer>"
        COMMENT "Copying Embree runtime DLLs...")

    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENVKL_PREFIX}/bin"
            "$<TARGET_FILE_DIR:QtOsprayViewer>"
        COMMENT "Copying OpenVKL runtime DLLs...")

    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BRLCAD_PREFIX}/bin"
            "$<TARGET_FILE_DIR:QtOsprayViewer>"
        COMMENT "Copying BRL-CAD runtime DLLs...")

    if(EXISTS "${TBB_REDIST_DIR}")
        add_custom_command(TARGET QtOsprayViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TBB_REDIST_DIR}"
                "$<TARGET_FILE_DIR:QtOsprayViewer>"
            COMMENT "Copying TBB runtime DLLs...")
    endif()

    if(BRLCAD_OSPRAY_MODULE_DLL AND EXISTS "${BRLCAD_OSPRAY_MODULE_DLL}")
        add_custom_command(TARGET QtOsprayViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BRLCAD_OSPRAY_MODULE_DLL}"
                "$<TARGET_FILE_DIR:QtOsprayViewer>/ospray_module_brl_cad.dll"
            COMMENT "Deploying ospray_module_brl_cad.dll...")
    else()
        message(WARNING
            "ospray_module_brl_cad.dll was not found. "
            "Set -DBRLCAD_OSPRAY_MODULE_DLL=<full path> so ospLoadModule(\"brl_cad\") can succeed at runtime.")
    endif()

    if(BRLCAD_OSPRAY_MODULE_DEPLOY_DIR AND EXISTS "${BRLCAD_OSPRAY_MODULE_DEPLOY_DIR}")
        add_custom_command(TARGET QtOsprayViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${BRLCAD_OSPRAY_MODULE_DEPLOY_DIR}"
                "$<TARGET_FILE_DIR:QtOsprayViewer>"
            COMMENT "Copying extra BRL-CAD module dependency DLLs...")
    endif()

    # Force the correct rkcommon.dll and embree4.dll — OpenVKL ships older
    # mismatched versions that get copied last and would override them.
    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${RKCOMMON_PREFIX}/bin/rkcommon.dll"
            "$<TARGET_FILE_DIR:QtOsprayViewer>/rkcommon.dll"
        COMMENT "Restoring correct rkcommon.dll...")
    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${EMBREE_PREFIX}/bin/embree4.dll"
            "$<TARGET_FILE_DIR:QtOsprayViewer>/embree4.dll"
        COMMENT "Restoring correct embree4.dll...")

    # Remove any CRT DLLs copied from third-party packages — always use the
    # system-installed versions to avoid version mismatches with Qt / MSVC.
    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:QtOsprayViewer>/vcruntime140.dll"
            "$<TARGET_FILE_DIR:QtOsprayViewer>/vcruntime140_1.dll"
            "$<TARGET_FILE_DIR:QtOsprayViewer>/msvcp140.dll"
        COMMENT "Removing bundled CRT DLLs (use system versions)...")

    # Find and run windeployqt from the Qt installation used at build time.
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND "${WINDEPLOYQT}" "$<TARGET_FILE:QtOsprayViewer>"
        COMMENT "Running windeployqt...")

    add_dependencies(QtOsprayViewer IBRTRenderWorker)
    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:IBRTRenderWorker>"
            "$<TARGET_FILE_DIR:QtOsprayViewer>/IBRTRenderWorker.exe"
        COMMENT "Deploying render worker executable...")

    add_dependencies(IBRTTests IBRTRenderWorker)
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OSPRAY_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTTests>"
        COMMENT "Copying OSPRay runtime DLLs for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RKCOMMON_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTTests>"
        COMMENT "Copying rkcommon runtime DLLs for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EMBREE_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTTests>"
        COMMENT "Copying Embree runtime DLLs for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENVKL_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTTests>"
        COMMENT "Copying OpenVKL runtime DLLs for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BRLCAD_PREFIX}/bin"
            "$<TARGET_FILE_DIR:IBRTTests>"
        COMMENT "Copying BRL-CAD runtime DLLs for tests...")
    if(EXISTS "${TBB_REDIST_DIR}")
        add_custom_command(TARGET IBRTTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TBB_REDIST_DIR}"
                "$<TARGET_FILE_DIR:IBRTTests>"
            COMMENT "Copying TBB runtime DLLs for tests...")
    endif()
    if(BRLCAD_OSPRAY_MODULE_DLL AND EXISTS "${BRLCAD_OSPRAY_MODULE_DLL}")
        add_custom_command(TARGET IBRTTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BRLCAD_OSPRAY_MODULE_DLL}"
                "$<TARGET_FILE_DIR:IBRTTests>/ospray_module_brl_cad.dll"
            COMMENT "Deploying ospray_module_brl_cad.dll for tests...")
    endif()
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${RKCOMMON_PREFIX}/bin/rkcommon.dll"
            "$<TARGET_FILE_DIR:IBRTTests>/rkcommon.dll"
        COMMENT "Restoring correct rkcommon.dll for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${EMBREE_PREFIX}/bin/embree4.dll"
            "$<TARGET_FILE_DIR:IBRTTests>/embree4.dll"
        COMMENT "Restoring correct embree4.dll for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:IBRTTests>/vcruntime140.dll"
            "$<TARGET_FILE_DIR:IBRTTests>/vcruntime140_1.dll"
            "$<TARGET_FILE_DIR:IBRTTests>/msvcp140.dll"
        COMMENT "Removing bundled CRT DLLs for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:IBRTRenderWorker>"
            "$<TARGET_FILE_DIR:IBRTTests>/IBRTRenderWorker.exe"
        COMMENT "Deploying render worker executable for tests...")
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND "${WINDEPLOYQT}" "$<TARGET_FILE:IBRTTests>"
        COMMENT "Running windeployqt for tests...")
elseif((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin") AND TARGET IBRTRenderWorker)
    add_dependencies(QtOsprayViewer IBRTRenderWorker)
    add_custom_command(TARGET QtOsprayViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:IBRTRenderWorker>"
            "$<TARGET_FILE_DIR:QtOsprayViewer>/IBRTRenderWorker"
        COMMENT "Deploying render worker executable...")

    add_dependencies(IBRTTests IBRTRenderWorker)
    add_custom_command(TARGET IBRTTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:IBRTRenderWorker>"
            "$<TARGET_FILE_DIR:IBRTTests>/IBRTRenderWorker"
        COMMENT "Deploying render worker executable for tests...")
elseif(WIN32)
    message(STATUS "IBRT render worker target is disabled; the viewer will use local rendering only.")
endif()

add_test(NAME IBRTTests COMMAND IBRTTests)
