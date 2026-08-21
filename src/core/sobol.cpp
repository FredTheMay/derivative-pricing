#include "mcd/core/sobol.hpp"

#include <array>

namespace mcd {

namespace {

constexpr unsigned kBits = 32;

struct PolyDef {
    unsigned degree; // 0 for the trivial (dimension 0) van der Corput case.
    // Bit (j-1) is coefficient a_j, for j = 1..degree-1 (a_0 and a_degree are always 1,
    // implicit for a degree-`degree` primitive polynomial over GF(2)).
    std::uint32_t interior_coeffs;
};

// Primitive polynomials, degrees 1-6, each independently verified this session by direct
// LFSR maximal-period simulation (period == 2^degree - 1 from a nonzero seed) -- not
// copied from an external Sobol direction-number table. See
// docs/design/10-sobol-qmc.md sec.2.
constexpr std::array<PolyDef, kSobolMaxDimensions> kPolynomials{{
    {0, 0},         // dimension 0: trivial (van der Corput)
    {1, 0b0},       // x + 1
    {2, 0b1},       // x^2 + x + 1        (a_1=1)
    {3, 0b01},      // x^3 + x + 1        (a_1=1, a_2=0)
    {4, 0b001},     // x^4 + x + 1        (a_1=1, a_2=0, a_3=0)
    {5, 0b0010},    // x^5 + x^2 + 1      (a_1=0, a_2=1, a_3=0, a_4=0)
    {6, 0b00001},   // x^6 + x + 1        (a_1=1, a_2=0, a_3=0, a_4=0, a_5=0)
}};

using DirectionTable = std::array<std::uint32_t, kBits + 1>; // 1-indexed; [0] unused.

// Bratley-Fox recurrence (Bratley & Fox 1988, "Algorithm 659"): m_k for k > degree is
// built from the polynomial's interior coefficients and the previous `degree` direction
// numbers. Initial direction numbers m_1..m_degree are all set to 1 -- always a valid
// choice (the only formal requirement is 0 < m_i < 2^i and m_i odd), trading
// discrepancy-optimality for a construction verifiable from first principles rather than
// an externally-sourced table. See docs/design/10-sobol-qmc.md sec.2.
DirectionTable compute_direction_numbers(const PolyDef& poly) {
    std::array<std::uint32_t, kBits + 1> m{};
    const unsigned s = poly.degree;

    if (s == 0) {
        for (unsigned k = 1; k <= kBits; ++k) {
            m[k] = 1;
        }
    } else {
        for (unsigned k = 1; k <= s; ++k) {
            m[k] = 1;
        }
        for (unsigned k = s + 1; k <= kBits; ++k) {
            std::uint32_t mk = m[k - s] ^ (m[k - s] << s);
            for (unsigned j = 1; j < s; ++j) {
                if (((poly.interior_coeffs >> (j - 1)) & 1U) != 0U) {
                    mk ^= (m[k - j] << j);
                }
            }
            m[k] = mk;
        }
    }

    DirectionTable v{};
    for (unsigned k = 1; k <= kBits; ++k) {
        v[k] = m[k] << (kBits - k);
    }
    return v;
}

const std::array<DirectionTable, kSobolMaxDimensions>& direction_tables() {
    static const auto tables = [] {
        std::array<DirectionTable, kSobolMaxDimensions> t{};
        for (unsigned d = 0; d < kSobolMaxDimensions; ++d) {
            t[d] = compute_direction_numbers(kPolynomials[d]);
        }
        return t;
    }();
    return tables;
}

} // namespace

double sobol_point(unsigned dimension, std::uint64_t index) noexcept {
    if (index == 0) {
        return 0.0;
    }
    const DirectionTable& v = direction_tables()[dimension];
    const std::uint64_t gray = index ^ (index >> 1U);

    std::uint32_t x = 0;
    for (unsigned b = 0; b < kBits; ++b) {
        if (((gray >> b) & 1ULL) != 0ULL) {
            x ^= v[b + 1];
        }
    }
    return static_cast<double>(x) / static_cast<double>(std::uint64_t{1} << kBits);
}

} // namespace mcd
