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
)

target_link_libraries(mcd_tests PRIVATE
    mcd_core
    mcd_warnings
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mcd_tests)
