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

find_package(Threads REQUIRED)

add_library(mcd_core
    src/core/types.cpp
    src/core/normal.cpp
    src/core/rng.cpp
    src/core/thread_pool.cpp
    src/core/linalg.cpp
    src/core/json.cpp
    src/core/sobol.cpp
    src/core/rng_simd.cpp
    src/core/gauss_legendre.cpp
    src/models/gbm.cpp
    src/models/brownian_bridge_path.cpp
    src/pricers/analytic.cpp
    src/pricers/binomial.cpp
    src/pricers/monte_carlo.cpp
    src/pricers/lsm.cpp
    src/pricers/qmc.cpp
    src/pricers/heston.cpp
    src/greeks/finite_difference.cpp
    src/greeks/likelihood_ratio.cpp
    src/greeks/pathwise.cpp
)
target_include_directories(mcd_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(mcd_core PUBLIC cxx_std_20)
target_compile_definitions(mcd_core PRIVATE MCD_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(mcd_core PRIVATE mcd_warnings PUBLIC Threads::Threads)

# Stretch Goal 4 (docs/design/11-simd.md sec.4): the scalar inverse CDF
# (normal.cpp) and its NEON counterpart (rng_simd.cpp) must be bitwise identical.
# FP contraction (auto fused-multiply-add) would let the compiler round one path's
# Horner evaluation differently from the other's, so it's disabled explicitly for
# just these two files -- not project-wide, and unrelated to -ffast-math.
set_source_files_properties(src/core/normal.cpp src/core/rng_simd.cpp
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off"
)

add_subdirectory(apps/mcd_cli)

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
    # Google Benchmark's own build enables -Werror on its own sources by default;
    # this only affects how *their* code is compiled, not ours (mcd_warnings, with
    # its own -Werror controlled by MCD_WARNINGS_AS_ERRORS, is unaffected). Disabled
    # because a sufficiently new compiler can trip warnings in benchmark.h that
    # didn't exist when v1.9.1 was released -- not something this project's own
    # code quality bar should be blocked on.
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(bench)
endif()
