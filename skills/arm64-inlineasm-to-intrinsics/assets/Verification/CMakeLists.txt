cmake_minimum_required(VERSION 3.20)
project(my_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------
# GoogleTest
# ----------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)

# Windows + clang-cl
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

set(COMMON_INCLUDE_DIRS
    include
)

add_library(kernel_compare STATIC
    src/kernel_intrinsics.cpp
    src/kernel_reference.cpp
)

target_include_directories(kernel_compare PUBLIC ${COMMON_INCLUDE_DIRS})

# ----------------------------
# tests
# ----------------------------
enable_testing()

add_executable(unit_tests
    tests/test_kernel_compare.cpp
)

target_include_directories(unit_tests
    PRIVATE
    ${COMMON_INCLUDE_DIRS}
)

target_link_libraries(unit_tests
    PRIVATE
    kernel_compare
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(unit_tests)