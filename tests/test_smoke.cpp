#include <gtest/gtest.h>

#include "retropp/version.h"

// Phase-0 smoke baseline (1/1/0): the engine target compiles, links, and runs
// from the consumer side. retropp::version() is contractually never empty.
TEST(Smoke, EngineVersionIsReachableAndNonEmpty) {
    EXPECT_FALSE(retropp::version().empty());
}
