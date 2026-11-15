// The cartridge the tests of an attack's statuses on one target read: the fixture tests/test_status_effect.cpp and tests/oracle/test_status_effect.cpp
// share.
//
// Test code. StatusEffectTest reads the cartridge FF6_VANILLA_ROM names once for the test binary; each test
// starts its own oracle on image().
#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "assets/cartridge.h"
#include "vanilla_rom.h"

namespace ostinato::test {

class StatusEffectTest : public ::testing::Test {
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
