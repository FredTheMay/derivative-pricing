add_executable(mcd_tests
    version_test.cpp
)

target_link_libraries(mcd_tests PRIVATE
    mcd_core
    mcd_warnings
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mcd_tests)
