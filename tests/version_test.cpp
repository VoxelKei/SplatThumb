#include <gtest/gtest.h>

#include "splatthumb/version.h"

TEST(Version, IsNotEmpty) {
    EXPECT_FALSE(splatthumb::version().empty());
}

