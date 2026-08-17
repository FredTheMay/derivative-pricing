add_executable(mcd_tests
    version_test.cpp
    analytic_test.cpp
    cfa_invariants_test.cpp
    rng_test.cpp
    stats_test.cpp
    monte_carlo_test.cpp
    exotics_mc_test.cpp
    variance_reduction_test.cpp
    convergence_test.cpp
    parallelism_test.cpp
    linalg_test.cpp
    greeks_test.cpp
    lsm_test.cpp
    json_test.cpp
    cli_test.cpp
)

target_link_libraries(mcd_tests PRIVATE
    mcd_core
    mcd_warnings
    GTest::gtest_main
)

# cli_test.cpp launches the real mcd_cli binary as a subprocess -- needs both the
# resolved path to it and a guarantee that it's actually built first (mcd_tests
# doesn't link mcd_cli, so CMake has no other reason to know about the dependency).
add_dependencies(mcd_tests mcd_cli)
target_compile_definitions(mcd_tests PRIVATE MCD_CLI_PATH="$<TARGET_FILE:mcd_cli>")

include(GoogleTest)
gtest_discover_tests(mcd_tests)
