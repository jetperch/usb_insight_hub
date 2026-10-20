# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

# Host (gcc) unit tests for the pure-C uih_core component.
#   cmake -S firmware/esp32s3/test/host -B build/host && cmake --build build/host
#   ctest --test-dir build/host --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(uih_core_host_tests C)
enable_testing()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -O0 -g --coverage)
add_link_options(--coverage)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/uih_core)
file(GLOB CORE_SOURCES ${CORE}/src/*.c)
add_library(uih_core STATIC ${CORE_SOURCES})
target_include_directories(uih_core PUBLIC ${CORE}/include)

get_filename_component(VECTORS ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test/vectors ABSOLUTE)

file(GLOB TESTS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(test_src ${TESTS})
    get_filename_component(name ${test_src} NAME_WE)
    add_executable(${name} ${test_src})
    target_link_libraries(${name} uih_core)
    target_compile_definitions(${name} PRIVATE UIH_VECTORS_DIR="${VECTORS}")
    add_test(NAME ${name} COMMAND ${name})
endforeach()
