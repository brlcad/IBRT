if(NOT IBRT_MANIFEST_WRITER OR NOT IBRT_MANIFEST_TEST_ROOT)
  message(FATAL_ERROR "The manifest writer and test root are required")
endif()

file(REMOVE_RECURSE "${IBRT_MANIFEST_TEST_ROOT}")
set(CMAKE_INSTALL_PREFIX "${IBRT_MANIFEST_TEST_ROOT}/install")
file(MAKE_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin" "${CMAKE_INSTALL_PREFIX}/lib")
file(WRITE "${CMAKE_INSTALL_PREFIX}/bin/IBRT" "viewer\n")
file(WRITE "${CMAKE_INSTALL_PREFIX}/lib/module.dll" "module\n")

set(IBRT_BUNDLE_MANIFEST "${IBRT_MANIFEST_TEST_ROOT}/configured/ibrt.json")
set(IBRT_BUNDLE_PLATFORM "TestOS")
set(expected_prefix "${IBRT_MANIFEST_TEST_ROOT}/build\tquoted\"prefix")
set(IBRT_BUNDLE_INSTALL_PREFIXES "${expected_prefix}")
set(CMAKE_INSTALL_MANIFEST_FILES
  "${CMAKE_INSTALL_PREFIX}/lib/module.dll"
  "${CMAKE_INSTALL_PREFIX}/bin/IBRT"
)
include("${IBRT_MANIFEST_WRITER}")

file(READ "${IBRT_BUNDLE_MANIFEST}" manifest)
string(JSON schema GET "${manifest}" schema)
string(JSON platform GET "${manifest}" platform)
string(JSON install_root GET "${manifest}" install_root)
string(JSON install_prefix GET "${manifest}" install_prefixes 0)
string(JSON file_count LENGTH "${manifest}" files)
string(JSON first_file GET "${manifest}" files 0)
if(NOT schema STREQUAL "1" OR NOT platform STREQUAL "TestOS" OR
    NOT install_root STREQUAL "${CMAKE_INSTALL_PREFIX}" OR
    NOT install_prefix STREQUAL "${expected_prefix}" OR
    NOT file_count EQUAL 2 OR NOT first_file STREQUAL "bin/IBRT")
  message(FATAL_ERROR "Unexpected IBRT bundle manifest:\n${manifest}")
endif()
