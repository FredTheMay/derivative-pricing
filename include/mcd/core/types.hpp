#pragma once

#include <cstdint>
#include <string_view>

namespace mcd {

// Library version string, e.g. "0.1.0". Populated from the CMake project
// version so it never drifts from CMakeLists.txt.
[[nodiscard]] std::string_view version() noexcept;

enum class OptionType : std::uint8_t { Call, Put };
enum class BarrierDirection : std::uint8_t { Up, Down };
enum class BarrierKnock : std::uint8_t { In, Out };
enum class StrikeStyle : std::uint8_t { Fixed, Floating };
enum class DigitalStyle : std::uint8_t { CashOrNothing, AssetOrNothing };

} // namespace mcd
