cmake_minimum_required(VERSION 3.25)

project(mcd
    VERSION 0.1.0
    DESCRIPTION "Monte Carlo derivatives pricing and risk engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MCD_ENABLE_TESTING "Build the GoogleTest test suite" ON)
option(MCD_ENABLE_BENCHMARKS "Fetch and build Google Benchmark" ON)
option(MCD_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(mcd_warnings INTERFACE)
target_compile_options(mcd_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    $<$<BOOL:${MCD_WARNINGS_AS_ERRORS}>:-Werror>
)

add_library(mcd_core
    src/core/types.cpp
    src/core/normal.cpp
    src/pricers/analytic.cpp
    src/pricers/binomial.cpp
)
target_include_directories(mcd_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mcd_core PUBLIC cxx_std_20)
target_compile_definitions(mcd_core PRIVATE MCD_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(mcd_core PRIVATE mcd_warnings)

if(MCD_ENABLE_TESTING)
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()

if(MCD_ENABLE_BENCHMARKS)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    FetchContent_MakeAvailable(benchmark)
    # No benchmark targets yet: the scaling/microbenchmark harness begins in
    # Phase 2. This only proves the FetchContent wiring works end to end.
endif()
