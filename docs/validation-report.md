# Validation Report

This report is generated incrementally as each phase completes. Every number in
it must come from a benchmark or test actually executed in this repository —
never fabricated or estimated. Sections below are populated starting Phase 1
(analytic validation) and Phase 3 (variance reduction, convergence plots).

## Status

- Phase 0 (scaffold): complete, no numerical content.
- Phase 1 (analytic layer and CFA invariants): not started.
- Phase 2 (single-threaded Monte Carlo core): not started.
- Phase 3 (exotics and variance reduction): not started.
- Phase 4 (parallelism): not started.
- Phase 5 (Greeks and American options): not started.
- Phase 6 (CLI, bindings, reporting): not started.
- Phase 7 (AWS demo): not started.

## Sections (populated as phases land)

- CFA invariant results table
- Convergence plots (log-log RMSE vs. paths, slope fit)
- Variance-reduction factor table (antithetic, control variate, Brownian bridge)
- Thread-scaling curve and Amdahl serial-fraction fit
- False-sharing A/B benchmark
- Bump-size sensitivity study (finite-difference Greeks)
- LSM American option validation against binomial reference
- Known limitations (documented, not hidden): LSM low-bias estimator; gamma for
  discontinuous payoffs under finite differences
