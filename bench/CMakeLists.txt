add_executable(mcd_bench
    mc_bench.cpp
)

target_link_libraries(mcd_bench PRIVATE
    mcd_core
    mcd_warnings
    benchmark::benchmark
)
