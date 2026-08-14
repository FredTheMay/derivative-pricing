#pragma once

#include <string_view>

namespace mcd {

// Library version string, e.g. "0.1.0". Populated from the CMake project
// version so it never drifts from CMakeLists.txt.
[[nodiscard]] std::string_view version() noexcept;

} // namespace mcd
