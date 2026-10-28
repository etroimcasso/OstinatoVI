// The battle picture's tables, every row read back through its accessor and compared to the raw
// bytes in the fixture, plus rows traced by hand from btlgfx_main.asm.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

#include "data/battle_gfx_tables.h"
#include "fixtures/battle_gfx_tables_expected.h"

namespace ostinato {
namespace {

template <std::size_t N>
std::uint16_t wordAt(const std::array<std::uint8_t, N>& bytes, std::size_t index) {
    return static_cast<std::uint16_t>(bytes[2 * index] | bytes[2 * index + 1] << 8);
}

// --- frames and poses --------------------------------------------------------------------------------

TEST(BattleGfxTables, EveryFrameTileMatchesTheCartridge) {
    for (int frame = 0; frame < static_cast<int>(kCharacterFrameCount); ++frame) {
        const auto tiles = characterFrameTiles(frame);
        for (std::size_t t = 0; t < kCharacterFrameTileCount; ++t) {
            const std::size_t index = static_cast<std::size_t>(frame) * kCharacterFrameTileCount + t;
            EXPECT_EQ(tiles[t].raw, wordAt(test::kExpectedCharacterFrames, index))
                << "frame " << frame << " tile " << t;
        }
    }
}

// Only the fallen frame draws in the top row; every other frame leaves it blank.
TEST(BattleGfxTables, StandingFramesLeaveTheTopRowBlank) {
    for (int frame = 1; frame < static_cast<int>(kCharacterFrameCount); ++frame) {
        const auto tiles = characterFrameTiles(frame);
        EXPECT_TRUE(tiles[0].isBlank() && tiles[1].isBlank()) << "frame " << frame;
    }
    EXPECT_FALSE(characterFrameTiles(0)[0].isBlank());
}

// _c2c745 :42553-42554: the fallen frame, and frame 1 (sheet tiles 0-5 in order).
TEST(BattleGfxTables, HandTracedFrames) {
    const auto fallen = characterFrameTiles(0);
    EXPECT_EQ(fallen[0], SheetTile::at(87));
    EXPECT_EQ(fallen[4], SheetTile::at(89));
    EXPECT_TRUE(fallen[5].isBlank());
    EXPECT_EQ(fallen[6], SheetTile::at(92));
    EXPECT_TRUE(fallen[7].isBlank());
    const auto standing = characterFrameTiles(1);
    for (std::size_t t = 2; t < kCharacterFrameTileCount; ++t) {
        EXPECT_EQ(standing[t].tile(), static_cast<int>(t - 2)) << t;
    }
}

TEST(BattleGfxTables, EveryPoseStepMatchesTheCartridge) {
    for (int pose = 0; pose < static_cast<int>(kCharacterPoseCount); ++pose) {
        const auto steps = characterPoseFrames(pose);
        for (std::size_t s = 0; s < kCharacterPoseSteps; ++s) {
            const std::size_t index = static_cast<std::size_t>(pose) * kCharacterPoseSteps + s;
            EXPECT_EQ(steps[s].raw, test::kExpectedCharacterPoses[index]) << pose << " " << s;
        }
    }
}

// _c2c6a9 :42509-42515: pose 0 shows nothing, pose 3 is mirrored, pose 6 holds one frame.
TEST(BattleGfxTables, HandTracedPoses) {
    for (const PoseFrame step : characterPoseFrames(0)) {
        EXPECT_TRUE(step.isNone());
    }
    const auto mirrored = characterPoseFrames(3);
    EXPECT_TRUE(mirrored[0].isMirrored());
    EXPECT_EQ(mirrored[0].frameIndex(), 4);
    EXPECT_EQ(mirrored[2].frameIndex(), 6);
    for (const PoseFrame step : characterPoseFrames(6)) {
        EXPECT_FALSE(step.isMirrored());
        EXPECT_EQ(step.frameIndex(), 17);
    }
    // :42547: the last pose mixes mirrored and plain frames.
    const auto last = characterPoseFrames(38);
    EXPECT_EQ(last[0], PoseFrame::mirrored(26));
    EXPECT_EQ(last[2], PoseFrame::frame(7));
}

// Every step of every pose but the first names one of the 32 frames.
TEST(BattleGfxTables, EveryPoseStepIsAFrame) {
    for (int pose = 1; pose < static_cast<int>(kCharacterPoseCount); ++pose) {
        for (const PoseFrame step : characterPoseFrames(pose)) {
            ASSERT_FALSE(step.isNone()) << pose;
            EXPECT_LT(step.frameIndex(), static_cast<int>(kCharacterFrameCount)) << pose;
        }
    }
}

// --- palettes ------------------------------------------------------------------------------------------

TEST(BattleGfxTables, EveryPaletteMatchesTheCartridge) {
    const auto palettes = battleCharacterPalettes();
    ASSERT_EQ(palettes.size(), test::kExpectedCharacterPalettes.size());
    for (std::size_t i = 0; i < palettes.size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(palettes[i].graphics), i);
        EXPECT_EQ(static_cast<std::uint8_t>(palettes[i].palette), test::kExpectedCharacterPalettes[i])
            << i;
    }
}

// CharPalTbl :42720-42743.
TEST(BattleGfxTables, HandTracedPalettes) {
    EXPECT_EQ(battleCharacterPalette(CharacterGfxId::TERRA), BattleCharacterPalette::TERRA);
    EXPECT_EQ(battleCharacterPalette(CharacterGfxId::SHADOW), BattleCharacterPalette::SHADOW);
    EXPECT_EQ(battleCharacterPalette(CharacterGfxId::SOLDIER), BattleCharacterPalette::BROWN_SOLDIER);
    EXPECT_EQ(battleCharacterPalette(kGreenSoldierGraphics), BattleCharacterPalette::GREEN_SOLDIER);
    EXPECT_EQ(static_cast<int>(kGreenSoldierGraphics), 23);
}

// --- the party's places --------------------------------------------------------------------------------

TEST(BattleGfxTables, EveryPartySlotMatchesTheCartridge) {
    for (std::size_t layout = 0; layout < kBattleLayoutCount; ++layout) {
        const auto slots = partyPlacement(layout);
        for (std::size_t slot = 0; slot < kPartySlotCount; ++slot) {
            const std::size_t i = layout * kPartySlotCount + slot;
            EXPECT_EQ(slots[slot].y, test::kExpectedPartyY[i]) << i;
            EXPECT_EQ(slots[slot].angle, wordAt(test::kExpectedPartyAngles, i)) << i;
            EXPECT_EQ(static_cast<std::uint16_t>(slots[slot].backRowAngle),
                      wordAt(test::kExpectedBackRowAngles, i))
                << i;
            EXPECT_EQ(slots[slot].mirrored, test::kExpectedPartyMirrored[i] != 0) << i;
            EXPECT_EQ(slots[slot].handsSwapped, test::kExpectedPartyHandsSwapped[i] != 0) << i;
        }
    }
    for (std::size_t slot = 0; slot < kPartySlotCount; ++slot) {
        EXPECT_EQ(magitekTrainPartyY(slot), test::kExpectedPartyY[kBattleLayoutCount * kPartySlotCount + slot]);
    }
}

// CharYOffsetTbl, CharXAngleTbl, CharRowOffsetTbl, CharFlipTbl :42801-42820, :45268-45272.
TEST(BattleGfxTables, HandTracedPartyPlaces) {
    const auto front = partyPlacement(0);
    EXPECT_EQ(front[0].y, 68);
    EXPECT_EQ(front[3].y, 122);
    EXPECT_EQ(front[0].angle, 368);
    EXPECT_EQ(front[0].backRowAngle, 12);
    EXPECT_FALSE(front[0].mirrored);
    const auto back = partyPlacement(1);
    EXPECT_EQ(back[2].angle, 144);
    EXPECT_EQ(back[2].backRowAngle, -12);
    EXPECT_TRUE(back[2].mirrored);
    const auto pincer = partyPlacement(2);
    EXPECT_EQ(pincer[1].angle, 256);
    EXPECT_TRUE(pincer[0].mirrored);
    EXPECT_FALSE(pincer[1].mirrored);
    const auto side = partyPlacement(3);
    EXPECT_EQ(side[1].y, 122);
    EXPECT_EQ(side[2].backRowAngle, -12);
    EXPECT_TRUE(side[3].handsSwapped);
    EXPECT_EQ(magitekTrainPartyY(0), 80);
}

// --- waiting poses -------------------------------------------------------------------------------------

TEST(BattleGfxTables, EveryCommandsWaitingPoseMatchesTheCartridge) {
    for (std::size_t command = 0; command < kCommandWaitingPoseCount; ++command) {
        EXPECT_EQ(commandWaitingPose(static_cast<BattleCommandId>(command)),
                  test::kExpectedCommandWaitingPoses[command])
            << command;
    }
}

// _c2e49a :45303-45305: Magic, Summon and Health wait in pose 9, Magitek in pose 0, the rest in 11.
TEST(BattleGfxTables, HandTracedWaitingPoses) {
    EXPECT_EQ(commandWaitingPose(BattleCommandId::FIGHT), 11);
    EXPECT_EQ(commandWaitingPose(BattleCommandId::MAGIC), 9);
    EXPECT_EQ(commandWaitingPose(BattleCommandId::SUMMON), 9);
    EXPECT_EQ(commandWaitingPose(BattleCommandId::HEALTH), 9);
    EXPECT_EQ(commandWaitingPose(BattleCommandId::MAGITEK), 0);
    EXPECT_EQ(commandWaitingPose(static_cast<BattleCommandId>(40)), 11);
}

// --- damage numerals, gauge, messages, colosseum ---------------------------------------------------------

TEST(BattleGfxTables, EveryBounceFrameMatchesTheCartridge) {
    for (int frame = 0; frame < static_cast<int>(kDamageNumberFrames); ++frame) {
        EXPECT_EQ(damageNumberLift(frame), test::kExpectedDamageNumberBounce[static_cast<std::size_t>(frame)])
            << frame;
    }
    // DmgNumBounceTbl :42866-42870: up to 16, back to the ground at frame 23, a second bounce,
    // then still.
    EXPECT_EQ(damageNumberLift(9), 16);
    EXPECT_EQ(damageNumberLift(23), 0);
    EXPECT_EQ(damageNumberLift(30), 8);
    EXPECT_EQ(damageNumberLift(64), 0);
}

TEST(BattleGfxTables, EveryGaugeCellMatchesBothBuilds) {
    EXPECT_EQ(gaugeEmptyGlyph(Language::EN), test::kExpectedGaugeEmptyGlyphEn);
    EXPECT_EQ(gaugeEmptyGlyph(Language::JP), test::kExpectedGaugeEmptyGlyphJp);
    for (int step = 0; step < static_cast<int>(kGaugeSteps); ++step) {
        const auto fills = gaugeCellFills(step);
        for (std::size_t cell = 0; cell < kGaugeCells; ++cell) {
            const std::size_t i = static_cast<std::size_t>(step) * kGaugeCells + cell;
            EXPECT_EQ(gaugeEmptyGlyph(Language::EN) + fills[cell], test::kExpectedGaugeStepsEn[i]) << i;
            EXPECT_EQ(gaugeEmptyGlyph(Language::JP) + fills[cell], test::kExpectedGaugeStepsJp[i]) << i;
        }
    }
}

// Step n fills n + 1 columns from the left; the last step fills all four cells.
TEST(BattleGfxTables, TheGaugeFillsLeftToRight) {
    for (int step = 0; step < static_cast<int>(kGaugeSteps); ++step) {
        const auto fills = gaugeCellFills(step);
        int total = 0;
        for (std::size_t cell = 0; cell < kGaugeCells; ++cell) {
            total += fills[cell];
            if (cell > 0) {
                EXPECT_LE(fills[cell], fills[cell - 1]) << step;
            }
        }
        EXPECT_EQ(total, step + 1);
    }
    for (const std::uint8_t fill : gaugeCellFills(31)) {
        EXPECT_EQ(fill, kGaugeCellFull);
    }
}

TEST(BattleGfxTables, MessageHoldsMatchTheCartridge) {
    for (int setting = 0; setting < static_cast<int>(kMessageSpeeds); ++setting) {
        EXPECT_EQ(messageHoldFrames(setting), test::kExpectedMessageSpeeds[static_cast<std::size_t>(setting)]);
    }
    // MsgSpeedTbl :42900: 32 frames at the fastest, 16 more for each step.
    EXPECT_EQ(messageHoldFrames(0), 32);
    EXPECT_EQ(messageHoldFrames(7), 144);
}

TEST(BattleGfxTables, ColosseumOffsetsMatchTheCartridge) {
    for (std::size_t a = 1; a < test::kExpectedMonsterAlignOffsets.size(); ++a) {
        EXPECT_EQ(static_cast<std::uint8_t>(colosseumAlignOffset(static_cast<MonsterVerticalAlignment>(a))),
                  test::kExpectedMonsterAlignOffsets[a])
            << a;
    }
    // MonsterAlignOffset :2832 read at :2882.
    EXPECT_EQ(colosseumAlignOffset(MonsterVerticalAlignment::GROUND), 0);
    EXPECT_EQ(colosseumAlignOffset(MonsterVerticalAlignment::BURIED), -8);
    EXPECT_EQ(colosseumAlignOffset(MonsterVerticalAlignment::FLOATING), 8);
    EXPECT_EQ(colosseumAlignOffset(MonsterVerticalAlignment::FLYING), -40);
}

TEST(BattleGfxTables, NumeralGlyphsMatchBothBuilds) {
    const auto en = numeralGlyphs(Language::EN);
    const auto jp = numeralGlyphs(Language::JP);
    ASSERT_EQ(en.size(), kNumeralGlyphCount);
    ASSERT_EQ(jp.size(), kNumeralGlyphCount);
    for (std::size_t i = 0; i < kNumeralGlyphCount; ++i) {
        EXPECT_EQ(en[i].glyph, test::kExpectedNumeralGlyphsEn[i]) << i;
        EXPECT_EQ(jp[i].glyph, test::kExpectedNumeralGlyphsJp[i]) << i;
    }
}

// --- the windows ---------------------------------------------------------------------------------------

// The battle's menu art places the window tiles 32 tiles in.
constexpr int kBattleWindowTileBase = 0x20;

TEST(BattleGfxTables, WindowTilesMatchTheCartridge) {
    const auto wallpaper = battleWindowWallpaper();
    for (std::size_t row = 0; row < kBattleWindowPattern; ++row) {
        for (std::size_t column = 0; column < kBattleWindowPattern; ++column) {
            EXPECT_EQ(static_cast<int>(wallpaper[row].tiles[column]) + kBattleWindowTileBase,
                      test::kExpectedWindowWallpaper[row * kBattleWindowPattern + column]);
        }
    }
    const BattleWindowBorder& border = battleWindowBorder();
    const std::array<const std::array<WindowTile, kBattleWindowPattern>*, 5> parts = {
        &border.top, &border.bottom, &border.corners, &border.left, &border.right};
    for (std::size_t part = 0; part < parts.size(); ++part) {
        for (std::size_t i = 0; i < kBattleWindowPattern; ++i) {
            EXPECT_EQ(static_cast<int>((*parts[part])[i]) + kBattleWindowTileBase,
                      test::kExpectedWindowBorder[part * kBattleWindowPattern + i]);
        }
    }
    // WindowTileTbl :12517, WindowBorderTileTbl :12524-12526.
    EXPECT_EQ(wallpaper[0].tiles[0], WindowTile::WALLPAPER_R0_C3);
    EXPECT_EQ(border.top[0], WindowTile::EDGE_TOP_B);
    EXPECT_EQ(border.corners[3], WindowTile::CORNER_BOTTOM_RIGHT);
}

// A window or placement's place, from its cell, as the byte offset into its layer's map; every
// row on one layer must land on the same base address, and the two tables share their base for the
// menu windows.
template <typename Entry, std::size_t N>
void expectPlaces(std::span<const Entry> entries, const std::array<std::uint8_t, N>& raw,
                  std::size_t rowBytes, std::size_t addressAt, std::map<BattleTileLayer, int>& bases) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        const std::size_t at = i * rowBytes + addressAt;
        const int address = raw[at] | raw[at + 1] << 8;
        const int offset = 2 * (e.row * 32 + e.column);
        const auto found = bases.emplace(e.layer, address - offset).first;
        EXPECT_EQ(found->second, address - offset) << "row " << i;
    }
}

TEST(BattleGfxTables, WindowsMatchBothBuilds) {
    for (const Language language : {Language::EN, Language::JP}) {
        const auto& raw = language == Language::EN ? test::kExpectedBattleWindowsEn : test::kExpectedBattleWindowsJp;
        const auto windows = battleWindows(language);
        ASSERT_EQ(windows.size(), kBattleWindowCount);
        for (std::size_t i = 0; i < windows.size(); ++i) {
            EXPECT_EQ(static_cast<std::size_t>(windows[i].window), i);
            EXPECT_EQ(windows[i].width, raw[i * 4]) << i;
            EXPECT_EQ(windows[i].height, raw[i * 4 + 1]) << i;
        }
        const auto& placementRaw =
            language == Language::EN ? test::kExpectedTextPlacementsEn : test::kExpectedTextPlacementsJp;
        const auto placements = battleTextPlacements(language);
        ASSERT_EQ(placements.size(), kBattleTextPlacementCount);
        for (std::size_t i = 0; i < placements.size(); ++i) {
            EXPECT_EQ(static_cast<std::size_t>(placements[i].placement), i);
            EXPECT_EQ(placements[i].width * 2, placementRaw[i * 8 + 4]) << i;
            EXPECT_EQ(placements[i].height, placementRaw[i * 8 + 6]) << i;
        }
        std::map<BattleTileLayer, int> bases;
        expectPlaces(windows, raw, 4, 2, bases);
        expectPlaces(placements, placementRaw, 8, 2, bases);
        EXPECT_EQ(bases.size(), 3u);
    }
}

// MenuWindowTbl :44663-44680, _c2df9e :44837-44840.
TEST(BattleGfxTables, HandTracedWindows) {
    const BattleWindowEntry& monsters = battleWindow(BattleWindowId::MONSTER_NAMES, Language::EN);
    EXPECT_EQ(monsters.column, 1);
    EXPECT_EQ(monsters.width, 12);
    EXPECT_EQ(monsters.height, 8);
    const BattleWindowEntry& party = battleWindow(BattleWindowId::PARTY_STATUS, Language::EN);
    EXPECT_EQ(party.column, 13);
    EXPECT_EQ(party.width, 18);
    const BattleWindowEntry& wide = battleWindow(BattleWindowId::WIDE_MESSAGE, Language::EN);
    EXPECT_EQ(wide.layer, BattleTileLayer::BATTLEFIELD);
    EXPECT_EQ(wide.column, 1);
    EXPECT_EQ(wide.row, 1);
    EXPECT_EQ(wide.width, 30);
    const BattleWindowEntry& narrow = battleWindow(BattleWindowId::NARROW_MESSAGE, Language::EN);
    EXPECT_EQ(narrow.column, 7);
    EXPECT_EQ(narrow.width, 18);
    EXPECT_EQ(battleWindow(BattleWindowId::WIDE_MESSAGE, Language::JP).width, 28);

    const BattleTextPlacementEntry& hp = battleTextPlacement(BattleTextPlacementId::HP, Language::EN);
    EXPECT_EQ(hp.layer, BattleTileLayer::MENU_TEXT);
    EXPECT_EQ(hp.column, 21);
    EXPECT_EQ(hp.width, 4);
    EXPECT_EQ(battleTextPlacement(BattleTextPlacementId::MONSTER_NAMES, Language::EN).column, 2);
    EXPECT_EQ(battleTextPlacement(BattleTextPlacementId::CHARACTER_NAMES, Language::EN).column, 14);
    EXPECT_EQ(battleTextPlacement(BattleTextPlacementId::GAUGES, Language::EN).column, 25);
}

// --- name kinds ----------------------------------------------------------------------------------------

TEST(BattleGfxTables, NameKindsMatchTheCartridge) {
    for (int kind = 0; kind < static_cast<int>(kNameKindCount); ++kind) {
        EXPECT_EQ(nameKindText(kind).raw, test::kExpectedNameKinds[static_cast<std::size_t>(kind)]);
    }
    // _c197a1 :22774, read by GfxCmd_01 :22788 and _c2bb11 :40904.
    EXPECT_EQ(nameKindText(0).command(), BattleTextCommand::ATTACK_NAME);
    EXPECT_EQ(nameKindText(1).command(), BattleTextCommand::ITEM_NAME);
    EXPECT_TRUE(nameKindText(2).isEsperAttackName());
    EXPECT_FALSE(nameKindText(2).command().has_value());
    EXPECT_TRUE(nameKindText(3).isNothing());
    EXPECT_TRUE(nameKindText(4).isBushidoName());
    EXPECT_EQ(nameKindText(5).command(), BattleTextCommand::COMMAND_NAME);
}

}  // namespace
}  // namespace ostinato
