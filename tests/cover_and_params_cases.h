// The cartridge the cover's and the animation set up's tests on the oracle read: the fixture
// tests/test_cover_and_params.cpp and tests/oracle/test_cover_and_params.cpp share.
//
// Test code. CoverAndParamsTest reads the cartridge FF6_VANILLA_ROM names once for the test binary;
// each test starts its own oracle on image().
#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "assets/cartridge.h"
#include "vanilla_rom.h"

namespace ostinato::test {

class CoverAndParamsTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    inline static std::vector<std::uint8_t>* image_ = nullptr;
};

}  // namespace ostinato::test
