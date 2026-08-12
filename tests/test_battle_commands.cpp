// Full-corpus tests of the battle-command data: BattleCmdProp (the 30 command
// property records) and the command-domain satellite tables — the three
// membership masks, the three command-id lists, the two per-command mappings
// (ATB delay, targeting-init byte), and the two pair tables (relic swaps,
// command->base attack). The byte-equivalence tests assert EVERY modeled entry
// matches the ROM bytes and that the two unused padding rows carry their known
// bytes; the semantic tests exercise the GetBitPtr-order membership, the
// CommandTargetingInit field decode, and the accessors.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/battle_commands.h"

#include "ostinato/attack_id.h"
#include "ostinato/battle_command_flags.h"
#include "ostinato/battle_command_id.h"

#include "fixtures/battle_cmd_prop_expected.h"
#include "fixtures/battle_cmd_tables_expected.h"

namespace {

using ostinato::BattleCommandId;
using ostinato::BattleCommandFlags;

std::uint8_t u8(BattleCommandId c) { return static_cast<std::uint8_t>(c); }

// The 30 command property records are byte-identical to the ROM, in command-id
// order; the two unused ROM rows ($1E/$1F) carry the NONE/MENU padding bytes.
TEST(BattleCmdProp, RecordsAreByteIdenticalToRom) {
    const auto table = ostinato::battleCommandProperties();
    ASSERT_EQ(table.size(), 30u);
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(ostinato::test::kExpectedBattleCmdProp[i].index, i);
        EXPECT_EQ(u8(table[i].command), i) << "command order at " << i;
        EXPECT_EQ(std::memcmp(&table[i].record,
                              ostinato::test::kExpectedBattleCmdProp[i].bytes.data(),
                              sizeof(table[i].record)),
                  0) << "record " << i;
    }
    // Rows $1E/$1F: unused NONE(0x00)/MENU(0xFF) padding, verified not modeled.
    for (std::size_t pad = 30; pad < 32; ++pad) {
        EXPECT_EQ(ostinato::test::kExpectedBattleCmdProp[pad].bytes[0], 0x00);
        EXPECT_EQ(ostinato::test::kExpectedBattleCmdProp[pad].bytes[1], 0xFF);
    }
}

// The three command-membership masks are byte-identical to the ROM four-byte
// masks.
TEST(BattleCommandMasks, AreByteIdenticalToRom) {
    EXPECT_EQ(std::memcmp(&ostinato::kConfusedAllowedCommands,
                          ostinato::test::kExpectedConfusedCmd.data(), 4), 0);
    EXPECT_EQ(std::memcmp(&ostinato::kBerserkAllowedCommands,
                          ostinato::test::kExpectedBerserkCmd.data(), 4), 0);
    EXPECT_EQ(std::memcmp(&ostinato::kRetargetCommands,
                          ostinato::test::kExpectedRetargetCmd.data(), 4), 0);
}

// has() reads the GetBitPtr bit order (command n -> byte n/8, bit n%8).
TEST(BattleCommandMasks, MembershipUsesGetBitPtrOrder) {
    EXPECT_TRUE(ostinato::kConfusedAllowedCommands.has(BattleCommandId::FIGHT));
    EXPECT_TRUE(ostinato::kConfusedAllowedCommands.has(BattleCommandId::MAGITEK));
    EXPECT_FALSE(ostinato::kConfusedAllowedCommands.has(BattleCommandId::ITEM));

    EXPECT_TRUE(ostinato::kBerserkAllowedCommands.has(BattleCommandId::RAGE));
    EXPECT_TRUE(ostinato::kBerserkAllowedCommands.has(BattleCommandId::JUMP));
    EXPECT_FALSE(ostinato::kBerserkAllowedCommands.has(BattleCommandId::MAGIC));

    EXPECT_TRUE(ostinato::kRetargetCommands.has(BattleCommandId::DANCE));
    EXPECT_TRUE(ostinato::kRetargetCommands.has(BattleCommandId::BUSHIDO));
    EXPECT_FALSE(ostinato::kRetargetCommands.has(BattleCommandId::FIGHT));
}

// The three command-id lists match the ROM bytes; each value names its command.
TEST(BattleCommandLists, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kRandomHandlerCommands.size(), 10u);
    for (std::size_t i = 0; i < ostinato::kRandomHandlerCommands.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kRandomHandlerCommands[i]),
                  ostinato::test::kExpectedRandCmdId[i]) << "rand " << i;
    }
    ASSERT_EQ(ostinato::kUpdateStateCommands.size(), 8u);
    for (std::size_t i = 0; i < ostinato::kUpdateStateCommands.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kUpdateStateCommands[i]),
                  ostinato::test::kExpectedUpdateCmdId[i]) << "update " << i;
    }
    ASSERT_EQ(ostinato::kInitFunctionCommands.size(), 6u);
    for (std::size_t i = 0; i < ostinato::kInitFunctionCommands.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kInitFunctionCommands[i]),
                  ostinato::test::kExpectedInitCmdId[i]) << "init " << i;
    }
}

// The per-command ATB delay mapping matches the ROM (30 modeled commands, in
// order); the two unreachable ROM bytes ($1E/$1F) are the $00 padding.
TEST(CommandAdvanceWait, IsByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kCommandAdvanceWait.size(), 30u);
    for (std::size_t i = 0; i < ostinato::kCommandAdvanceWait.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kCommandAdvanceWait[i].command), i);
        EXPECT_EQ(ostinato::kCommandAdvanceWait[i].wait,
                  ostinato::test::kExpectedCmdDelay[i]) << "delay " << i;
    }
    EXPECT_EQ(ostinato::test::kExpectedCmdDelay[30], 0x00);
    EXPECT_EQ(ostinato::test::kExpectedCmdDelay[31], 0x00);
}

// The per-command targeting-init mapping matches the ROM byte for byte.
TEST(CommandTargetingInitTable, IsByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kCommandTargetingInit.size(), 30u);
    for (std::size_t i = 0; i < ostinato::kCommandTargetingInit.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kCommandTargetingInit[i].command), i);
        EXPECT_EQ(ostinato::kCommandTargetingInit[i].init.bits,
                  ostinato::test::kExpectedCmdTarget[i]) << "target " << i;
    }
}

// The paired tables map each row's two named columns; assert each column
// against its own ROM table.
TEST(BattleCommandPairs, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kRelicCommandSwaps.size(), 5u);
    for (std::size_t i = 0; i < ostinato::kRelicCommandSwaps.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kRelicCommandSwaps[i].from),
                  ostinato::test::kExpectedRelicCmd1[i]) << "relic from " << i;
        EXPECT_EQ(u8(ostinato::kRelicCommandSwaps[i].to),
                  ostinato::test::kExpectedRelicCmd2[i]) << "relic to " << i;
    }
    ASSERT_EQ(ostinato::kCommandAttackBases.size(), 5u);
    for (std::size_t i = 0; i < ostinato::kCommandAttackBases.size(); ++i) {
        EXPECT_EQ(u8(ostinato::kCommandAttackBases[i].command),
                  ostinato::test::kExpectedCmdWithAttack[i]) << "cmd " << i;
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kCommandAttackBases[i].attackBase),
                  ostinato::test::kExpectedCmdAttackOffset[i]) << "attack " << i;
    }
}

// CommandTargetingInit splits its byte into three non-overlapping fields
// ($E1|$18|$06 == $FF): FIGHT($20) sets directBits, MORPH($18) sets
// initialTarget, THROW($06) sets dispatchIndex.
TEST(CommandTargetingInit, FieldDecode) {
    const auto fight = ostinato::commandTargetingInit(BattleCommandId::FIGHT);
    EXPECT_EQ(fight.directBits(), 0x20);
    EXPECT_EQ(fight.initialTarget(), 0x00);
    EXPECT_EQ(fight.dispatchIndex(), 0x00);

    const auto morph = ostinato::commandTargetingInit(BattleCommandId::MORPH);
    EXPECT_EQ(morph.directBits(), 0x00);
    EXPECT_EQ(morph.initialTarget(), 0x18);
    EXPECT_EQ(morph.dispatchIndex(), 0x00);

    const auto th = ostinato::commandTargetingInit(BattleCommandId::THROW);
    EXPECT_EQ(th.directBits(), 0x00);
    EXPECT_EQ(th.initialTarget(), 0x00);
    EXPECT_EQ(th.dispatchIndex(), 0x06);
}

// The command accessors hand-trace to the ROM values.
TEST(BattleCommandAccessors, Trace) {
    const auto& fight = ostinato::battleCommandProperties(BattleCommandId::FIGHT);
    EXPECT_TRUE(fight.flags.has(BattleCommandFlags::GOGO));
    EXPECT_TRUE(fight.flags.has(BattleCommandFlags::IMP));
    EXPECT_EQ(fight.targeting.bits, 0x41);  // MANUAL | ENEMY

    const auto& item = ostinato::battleCommandProperties(BattleCommandId::ITEM);
    EXPECT_FALSE(item.flags.has(BattleCommandFlags::UNKNOWN));
    EXPECT_EQ(item.targeting.bits, 0xFF);  // MENU

    EXPECT_EQ(ostinato::commandAdvanceWait(BattleCommandId::JUMP), 224);
    EXPECT_EQ(ostinato::commandAdvanceWait(BattleCommandId::FIGHT), 16);
    EXPECT_EQ(ostinato::commandTargetingInit(BattleCommandId::SKETCH).bits, 0x80);
}

}  // namespace
