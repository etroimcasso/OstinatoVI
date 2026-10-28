#include "data/battle_gfx_tables.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

constexpr std::array<CharacterFrameEntry, kCharacterFrameCount> kCharacterFrames = {{
#include "data/generated/battle_character_frames_data.inc"
}};

constexpr std::array<CharacterPoseEntry, kCharacterPoseCount> kCharacterPoses = {{
#include "data/generated/battle_character_poses_data.inc"
}};

constexpr std::array<CharacterPaletteEntry, kBattleGraphicsCount> kCharacterPalettes = {{
#include "data/generated/battle_character_palettes_data.inc"
}};

constexpr std::array<PartyPlacementEntry, kBattleLayoutCount> kPartyPlacements = {{
#include "data/generated/battle_party_placement_data.inc"
}};

constexpr std::array<MagitekTrainPartyEntry, kPartySlotCount> kMagitekTrainParty = {{
#include "data/generated/battle_magitek_train_party_data.inc"
}};

// The commands with names, and the ones past them.
constexpr std::size_t kNamedCommandCount = static_cast<std::size_t>(BattleCommandId::MAGITEK) + 1;

constexpr std::array<CommandWaitingPoseEntry, kNamedCommandCount> kCommandWaitingPoses = {{
#include "data/generated/battle_command_waiting_poses_data.inc"
}};

constexpr std::array<UnnamedCommandWaitingPoseEntry, kCommandWaitingPoseCount - kNamedCommandCount>
    kUnnamedCommandWaitingPoses = {{
#include "data/generated/battle_unnamed_command_waiting_poses_data.inc"
}};

constexpr std::array<DamageNumberBounceEntry, kDamageNumberFrames> kDamageNumberBounce = {{
#include "data/generated/battle_damage_number_bounce_data.inc"
}};

constexpr std::array<GaugeStepEntry, kGaugeSteps> kGaugeStepCells = {{
#include "data/generated/battle_gauge_steps_data.inc"
}};

constexpr std::array<GaugeEmptyGlyphEntry, 2> kGaugeEmptyGlyphs = {{
#include "data/generated/battle_gauge_empty_glyph_data.inc"
}};

constexpr std::array<MessageSpeedEntry, kMessageSpeeds> kMessageSpeedFrames = {{
#include "data/generated/battle_message_speed_data.inc"
}};

constexpr std::array<MonsterAlignOffsetEntry, 5> kMonsterAlignOffsets = {{
#include "data/generated/battle_monster_align_offset_data.inc"
}};

constexpr std::array<NumeralGlyphEntry, kNumeralGlyphCount> kNumeralGlyphsEn = {{
#include "data/generated/battle_numeral_glyphs_en_data.inc"
}};

constexpr std::array<NumeralGlyphEntry, kNumeralGlyphCount> kNumeralGlyphsJp = {{
#include "data/generated/battle_numeral_glyphs_jp_data.inc"
}};

constexpr std::array<BattleWindowWallpaperRow, kBattleWindowPattern> kBattleWindowWallpaper = {{
#include "data/generated/battle_window_wallpaper_data.inc"
}};

constexpr BattleWindowBorder kBattleWindowBorder = {
#include "data/generated/battle_window_border_data.inc"
};

constexpr std::array<BattleWindowEntry, kBattleWindowCount> kBattleWindowsEn = {{
#include "data/generated/battle_windows_en_data.inc"
}};

constexpr std::array<BattleWindowEntry, kBattleWindowCount> kBattleWindowsJp = {{
#include "data/generated/battle_windows_jp_data.inc"
}};

constexpr std::array<BattleTextPlacementEntry, kBattleTextPlacementCount> kBattleTextPlacementsEn = {{
#include "data/generated/battle_text_placements_en_data.inc"
}};

constexpr std::array<BattleTextPlacementEntry, kBattleTextPlacementCount> kBattleTextPlacementsJp = {{
#include "data/generated/battle_text_placements_jp_data.inc"
}};

constexpr std::array<NameKindEntry, kNameKindCount> kNameKinds = {{
#include "data/generated/battle_name_kinds_data.inc"
}};

// Every table's identity field matches its row's position.
template <typename Table, typename Identity>
constexpr bool identitiesMatchPositions(const Table& table, Identity identity, std::size_t first = 0) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (static_cast<std::size_t>(identity(table[i])) != first + i) {
            return false;
        }
    }
    return true;
}

static_assert(identitiesMatchPositions(kCharacterFrames, [](const auto& e) { return e.frame; }),
              "kCharacterFrames frame fields must match array positions");
static_assert(identitiesMatchPositions(kCharacterPoses, [](const auto& e) { return e.pose; }),
              "kCharacterPoses pose fields must match array positions");
static_assert(identitiesMatchPositions(kCharacterPalettes, [](const auto& e) { return e.graphics; }),
              "kCharacterPalettes graphics fields must match array positions");
static_assert(identitiesMatchPositions(kPartyPlacements, [](const auto& e) { return e.layout; }),
              "kPartyPlacements layout fields must match array positions");
static_assert(identitiesMatchPositions(kMagitekTrainParty, [](const auto& e) { return e.slot; }),
              "kMagitekTrainParty slot fields must match array positions");
static_assert(identitiesMatchPositions(kCommandWaitingPoses, [](const auto& e) { return e.command; }),
              "kCommandWaitingPoses command fields must match array positions");
static_assert(identitiesMatchPositions(kUnnamedCommandWaitingPoses,
                                       [](const auto& e) { return e.command; }, kNamedCommandCount),
              "kUnnamedCommandWaitingPoses command fields must continue past the named commands");
static_assert(identitiesMatchPositions(kDamageNumberBounce, [](const auto& e) { return e.frame; }),
              "kDamageNumberBounce frame fields must match array positions");
static_assert(identitiesMatchPositions(kGaugeStepCells, [](const auto& e) { return e.step; }),
              "kGaugeStepCells step fields must match array positions");
static_assert(identitiesMatchPositions(kGaugeEmptyGlyphs, [](const auto& e) { return e.language; }),
              "kGaugeEmptyGlyphs language fields must match array positions");
static_assert(identitiesMatchPositions(kMessageSpeedFrames, [](const auto& e) { return e.setting; }),
              "kMessageSpeedFrames setting fields must match array positions");
static_assert(identitiesMatchPositions(kMonsterAlignOffsets, [](const auto& e) { return e.alignment; }),
              "kMonsterAlignOffsets alignment fields must match array positions");
static_assert(identitiesMatchPositions(kNumeralGlyphsEn, [](const auto& e) { return e.slot; }) &&
                  identitiesMatchPositions(kNumeralGlyphsJp, [](const auto& e) { return e.slot; }),
              "numeral glyph slot fields must match array positions");
static_assert(identitiesMatchPositions(kBattleWindowWallpaper, [](const auto& e) { return e.row; }),
              "kBattleWindowWallpaper row fields must match array positions");
static_assert(identitiesMatchPositions(kBattleWindowsEn, [](const auto& e) { return e.window; }) &&
                  identitiesMatchPositions(kBattleWindowsJp, [](const auto& e) { return e.window; }),
              "battle window fields must match array positions");
static_assert(identitiesMatchPositions(kBattleTextPlacementsEn,
                                       [](const auto& e) { return e.placement; }) &&
                  identitiesMatchPositions(kBattleTextPlacementsJp,
                                           [](const auto& e) { return e.placement; }),
              "text placement fields must match array positions");
static_assert(identitiesMatchPositions(kNameKinds, [](const auto& e) { return e.kind; }),
              "kNameKinds kind fields must match array positions");

}  // namespace

std::span<const SheetTile, kCharacterFrameTileCount> characterFrameTiles(int frame) {
    assert(frame >= 0 && static_cast<std::size_t>(frame) < kCharacterFrames.size() && "no such frame");
    return kCharacterFrames[static_cast<std::size_t>(frame)].tiles;
}

std::span<const CharacterFrameEntry> characterFrames() { return kCharacterFrames; }

std::span<const PoseFrame, kCharacterPoseSteps> characterPoseFrames(int pose) {
    assert(pose >= 0 && static_cast<std::size_t>(pose) < kCharacterPoses.size() && "no such pose");
    return kCharacterPoses[static_cast<std::size_t>(pose)].steps;
}

std::span<const CharacterPoseEntry> characterPoses() { return kCharacterPoses; }

BattleCharacterPalette battleCharacterPalette(CharacterGfxId graphics) {
    const auto index = static_cast<std::size_t>(graphics);
    assert(index < kCharacterPalettes.size() && "no battle palette for these graphics");
    return kCharacterPalettes[index].palette;
}

std::span<const CharacterPaletteEntry> battleCharacterPalettes() { return kCharacterPalettes; }

std::span<const PartySlotPlacement, kPartySlotCount> partyPlacement(std::size_t layout) {
    assert(layout < kPartyPlacements.size() && "no such battle layout");
    return kPartyPlacements[layout].slots;
}

std::span<const PartyPlacementEntry> partyPlacements() { return kPartyPlacements; }

std::uint8_t magitekTrainPartyY(std::size_t slot) {
    assert(slot < kMagitekTrainParty.size() && "no such party slot");
    return kMagitekTrainParty[slot].y;
}

std::uint8_t commandWaitingPose(BattleCommandId command) {
    const auto index = static_cast<std::size_t>(command);
    assert(index < kCommandWaitingPoseCount && "no waiting pose for this command");
    if (index < kNamedCommandCount) {
        return kCommandWaitingPoses[index].pose;
    }
    return kUnnamedCommandWaitingPoses[index - kNamedCommandCount].pose;
}

std::uint8_t damageNumberLift(int frame) {
    assert(frame >= 0 && static_cast<std::size_t>(frame) < kDamageNumberBounce.size() &&
           "past the numeral's last frame");
    return kDamageNumberBounce[static_cast<std::size_t>(frame)].lift;
}

std::span<const std::uint8_t, kGaugeCells> gaugeCellFills(int step) {
    assert(step >= 0 && static_cast<std::size_t>(step) < kGaugeStepCells.size() && "no such gauge step");
    return kGaugeStepCells[static_cast<std::size_t>(step)].cellFills;
}

std::uint8_t gaugeEmptyGlyph(Language language) {
    return kGaugeEmptyGlyphs[static_cast<std::size_t>(language)].glyph;
}

std::uint8_t messageHoldFrames(int setting) {
    assert(setting >= 0 && static_cast<std::size_t>(setting) < kMessageSpeedFrames.size() &&
           "no such message speed");
    return kMessageSpeedFrames[static_cast<std::size_t>(setting)].frames;
}

std::int8_t colosseumAlignOffset(MonsterVerticalAlignment alignment) {
    assert(alignment != MonsterVerticalAlignment::CEILING && "a ceiling monster has no offset");
    return kMonsterAlignOffsets[static_cast<std::size_t>(alignment)].yOffset;
}

std::span<const NumeralGlyphEntry> numeralGlyphs(Language language) {
    return language == Language::EN ? std::span<const NumeralGlyphEntry>{kNumeralGlyphsEn}
                                    : std::span<const NumeralGlyphEntry>{kNumeralGlyphsJp};
}

std::span<const BattleWindowWallpaperRow> battleWindowWallpaper() { return kBattleWindowWallpaper; }

const BattleWindowBorder& battleWindowBorder() { return kBattleWindowBorder; }

std::span<const BattleWindowEntry> battleWindows(Language language) {
    return language == Language::EN ? std::span<const BattleWindowEntry>{kBattleWindowsEn}
                                    : std::span<const BattleWindowEntry>{kBattleWindowsJp};
}

const BattleWindowEntry& battleWindow(BattleWindowId window, Language language) {
    return battleWindows(language)[static_cast<std::size_t>(window)];
}

std::span<const BattleTextPlacementEntry> battleTextPlacements(Language language) {
    return language == Language::EN ? std::span<const BattleTextPlacementEntry>{kBattleTextPlacementsEn}
                                    : std::span<const BattleTextPlacementEntry>{kBattleTextPlacementsJp};
}

const BattleTextPlacementEntry& battleTextPlacement(BattleTextPlacementId placement,
                                                    Language language) {
    return battleTextPlacements(language)[static_cast<std::size_t>(placement)];
}

NameKindText nameKindText(int kind) {
    assert(kind >= 0 && static_cast<std::size_t>(kind) < kNameKinds.size() && "no such name kind");
    return kNameKinds[static_cast<std::size_t>(kind)].text;
}

std::span<const NameKindEntry> nameKinds() { return kNameKinds; }

}  // namespace ostinato
