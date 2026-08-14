#pragma once

#include "mcd/core/types.hpp"

namespace mcd::pricers {

// European vanilla under GBM with continuous carry yield q (Black-Scholes-Merton).
[[nodiscard]] double black_scholes_merton(double spot, double strike, double rate,
                                           double carry_yield, double vol, double time,
                                           OptionType type) noexcept;

// Options on a futures/forward price (Black 1976).
[[nodiscard]] double black76(double forward, double strike, double rate, double vol, double time,
                              OptionType type) noexcept;

// Continuously-monitored geometric-average Asian, fixed strike (Kemna-Vorst 1984).
[[nodiscard]] double kemna_vorst(double spot, double strike, double rate, double carry_yield,
                                  double vol, double time, OptionType type) noexcept;

// Continuous single barrier, all eight direction x knock x OptionType combinations
// (Reiner-Rubinstein 1991). `barrier` is the knock level H; `rebate` is paid immediately
// on knock-out, or at expiry on a knock-in that never triggers.
[[nodiscard]] double reiner_rubinstein(double spot, double strike, double barrier, double rate,
                                        double carry_yield, double vol, double time,
                                        OptionType type, BarrierDirection direction,
                                        BarrierKnock knock, double rebate = 0.0) noexcept;

// Lookback, fixed strike: payoff compares the realized extremum to a fixed strike
// (Goldman-Sosin-Gatto 1979). Priced at inception, so the running extremum equals spot.
[[nodiscard]] double lookback_fixed_strike(double spot, double strike, double rate,
                                            double carry_yield, double vol, double time,
                                            OptionType type) noexcept;

// Lookback, floating strike: payoff compares terminal spot to the realized extremum.
[[nodiscard]] double lookback_floating_strike(double spot, double rate, double carry_yield,
                                               double vol, double time, OptionType type) noexcept;

// Digital / binary option.
[[nodiscard]] double digital(double spot, double strike, double rate, double carry_yield,
                              double vol, double time, OptionType type, DigitalStyle style,
                              double cash_amount = 1.0) noexcept;

// Cost-of-carry forward price: F0 = S0 * e^((r-q)*T).
[[nodiscard]] double forward_price(double spot, double rate, double carry_yield,
                                    double time) noexcept;

// Value of an existing forward contract during its life:
// Vt = (Ft - F0) * e^(-r*(T-t)), where Ft is the current fair forward price for the
// same maturity and F0 is the forward price locked in at initiation.
[[nodiscard]] double forward_value(double forward_now, double forward_at_inception, double rate,
                                    double time_to_expiry) noexcept;

// Undiscounted daily mark-to-market gain/loss on a futures position between two
// settlement prices.
[[nodiscard]] double futures_mark_to_market(double futures_price_new,
                                             double futures_price_old) noexcept;

} // namespace mcd::pricers
