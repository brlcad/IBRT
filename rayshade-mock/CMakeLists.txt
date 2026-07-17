cmake_minimum_required(VERSION 3.16)
project(rayshade LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rayshade src/RayShader.cpp)
target_include_directories(rayshade PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(rayshade_smoke examples/smoke.cpp)
target_link_libraries(rayshade_smoke PRIVATE rayshade)

enable_testing()
add_test(NAME smoke COMMAND rayshade_smoke)
