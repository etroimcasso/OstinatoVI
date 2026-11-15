// What the attack frame's tests on the oracle share: the cartridge they read, and a count of what a
// battle script shows. Included by tests/test_attack_oracle.cpp and tests/oracle/test_attack_oracle.cpp.
//
// Test code. AttackOracleTest reads the cartridge FF6_VANILLA_ROM names once for the test binary.
#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "vanilla_rom.h"

namespace ostinato::test {

class AttackOracleTest : public ::testing::Test {
public:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    inline static std::vector<std::uint8_t>* image_ = nullptr;
};

// How many times `what` appears in `text`.
inline int countOf(const std::string& text, const std::string& what) {
    int count = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) {
        ++count;
    }
    return count;
}

}  // namespace ostinato::test
