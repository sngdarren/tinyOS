cmake_minimum_required(VERSION 3.20)
project(tinyOS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # feeds clangd / IDE diagnostics

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Benchmarks are meaningless unoptimised, but keep frame pointers so profilers
# and stack traces still work.
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
    -fno-omit-frame-pointer
)

# GLOB so that new source files you add are picked up without editing this
# file. CONFIGURE_DEPENDS makes CMake re-scan when the file list changes.
file(GLOB_RECURSE TINYOS_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(tinyos_core ${TINYOS_SOURCES})
target_include_directories(tinyos_core PUBLIC include)

# --- tests ---------------------------------------------------------------
enable_testing()

file(GLOB TINYOS_TESTS CONFIGURE_DEPENDS tests/test_*.cpp)
foreach(test_src ${TINYOS_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE tinyos_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# --- benchmarks ----------------------------------------------------------
file(GLOB TINYOS_BENCHES CONFIGURE_DEPENDS bench/bench_*.cpp)
foreach(bench_src ${TINYOS_BENCHES})
    get_filename_component(bench_name ${bench_src} NAME_WE)
    add_executable(${bench_name} ${bench_src})
    target_link_libraries(${bench_name} PRIVATE tinyos_core)
endforeach()