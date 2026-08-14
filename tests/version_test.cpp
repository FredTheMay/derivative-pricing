#include "mcd/core/types.hpp"

#include <gtest/gtest.h>

TEST(Version, IsNonEmpty) { EXPECT_FALSE(mcd::version().empty()); }
