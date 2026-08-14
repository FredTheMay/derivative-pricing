# CFA Level I Derivatives — Executable Invariant Mapping

This document maps each invariant tested in `tests/cfa_invariants_test.cpp`
(Phase 1) to the CFA Level I Derivatives learning module it corresponds to,
described here in original wording. No curriculum text, tables, or exhibits are
reproduced — only module references and the underlying, public-domain
mathematics that this engine implements independently.

| Test | Module | Relationship, in my own words |
|---|---|---|
| `CostOfCarry` | LM4 | A forward price is the spot price grown at the net cost of holding the underlying to maturity — the risk-free rate less any yield the underlying pays out (dividends, foreign interest, storage-less carry). |
| `ForwardValueAtInitiationIsZero` | LM5 | A forward struck at the prevailing fair forward price commits neither side to an initial payment, so its value at that instant is zero by construction. |
| `ForwardValueDuringLife` | LM5 | Once time has passed, an existing forward's value is the discounted difference between the new fair forward price and the one originally locked in — the contract's value tracks how the market has moved against or in favor of the original terms. |
| `FuturesForwardEquivalenceUnderDeterministicRates` | LM6 | When interest rates are known in advance (as in this engine's model), daily futures resettlement has no extra value versus a single forward settlement, so the two prices coincide. |
| `ExerciseValuePlusTimeValueIsNonNegative` | LM8 | An option's value splits into what it would be worth if exercised immediately (never negative, since exercise is optional) and the extra value of retaining optionality until expiry. |
| `SixFactorSensitivitySigns` | LM8 | Six directional facts about how option value responds to its inputs: value rises with the underlying price for a call and falls for a put; value falls with a higher strike for a call and rises for a put; value rises with volatility for both, since more dispersion only helps an optional payoff. |
| `PutCallParity` | LM9 | A call plus a deposit that grows to the strike at expiry replicates a put plus holding the underlying (net of its yield) — so the two combinations must have equal cost, or an arbitrage exists. |
| `PutCallForwardParity` | LM9 | The same replication argument restated with a forward in place of the underlying position, since a forward is itself a levered, funded position in the underlying. |
| `BinomialRiskNeutrality` | LM10 | In a single-period up/down model, there is exactly one probability of the "up" outcome that makes the underlying's expected discounted growth equal the risk-free rate — that probability, not any real-world belief about which way the price will move, is what a no-arbitrage price must use. |
| `BinomialConvergesToBsm` | LM10 | As a discrete up/down tree is sliced into more and finer periods, its price converges to the continuous-time closed-form price — the tree is a discretization of the same underlying model, not a different one. |
| `NoArbitrageBounds` | LM4, LM8 | An option can never be worth less than its immediate exercise value net of financing, nor more than the underlying itself (net of any yield it pays out) — both are static replication arguments independent of any volatility assumption. |
| `MonotonicityInSpot`, `MonotonicityInVolatility`, `MonotonicityInStrike` | LM8 | A call's value moves in a single, predictable direction as each of these three inputs is swept, holding the others fixed — a direct, swept-grid restatement of the sensitivity signs above. |

## A limitation this engine surfaces beyond the curriculum's heuristic

The "time value is never negative" claim tested by `ExerciseValuePlusTimeValueIsNonNegative`
is a good approximation near the money but is not universally true for European options.
A deep in-the-money European put can price *below* its immediate exercise value: the
holder cannot actually exercise early to realize that value now, and discounting the
eventual payoff can leave less than today's intrinsic value. This engine's own
put-call-parity and no-arbitrage-bound tests confirm the pricer is correct in that
regime — it is the single-sentence CFA-level heuristic that is imprecise there, not the
model. This is exactly the kind of nuance the curriculum's introductory framing elides
and that this project's continuous-time implementation makes visible. American options
(Phase 5, Longstaff-Schwartz) do not have this issue, since early exercise really is
available as a floor.
