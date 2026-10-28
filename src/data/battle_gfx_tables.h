// The tables a battle's picture is placed and timed by: the frames and poses a party member is
// drawn in, each one's palette and the four status colors beside it, where each one's sprite sheet
// lies in the cartridge, where the party stands, the pose a party member waits in before acting,
// how a damage numeral bounces, the HUD's gauge cells, how long a message stays up, where a
// colosseum monster stands, the glyphs the numerals are cut from, the battle windows' tiles and
// places, where the HUD text sits, and what each kind of name in the narrow window reads. The rows
// are generated (src/data/generated/battle_*_data.inc); this header owns the row types and the
// lookups.
//
//     const PoseFrame step = characterPoseFrames(pose)[counter / 4 % 4];
//     for (const SheetTile tile : characterFrameTiles(step.frameIndex())) { ... }
//     const PartySlotPlacement& slot = partyPlacement(battleType)[1];
//     const BattleWindowEntry& box = battleWindow(BattleWindowId::PARTY_STATUS, Language::EN);
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ostinato/battle_character_palette.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/battle_text_command.h"
#include "ostinato/battle_text_placement_id.h"
#include "ostinato/battle_window_id.h"
#include "ostinato/character_gfx_id.h"
#include "ostinato/game_version.h"
#include "ostinato/monster_vertical_alignment.h"
#include "ostinato/window_tile.h"
#include "data/cartridge_color.h"

namespace ostinato {

// --- a party member's frames ----------------------------------------------------------------------

// A party member is drawn in one of 32 frames, each eight 8x8 tiles cut from the character's field
// sprite sheet. A standing frame is 16 pixels wide and 32 tall, its tiles in pairs top to bottom —
// tiles 0-1 the top row, 6-7 the bottom — with the top row blank, so what shows is 16 x 24. Frame 0
// is the party member fallen: two 16 x 16 halves side by side, tiles 0-3 the left half (0-1 above
// 2-3) and 4-7 the right.
inline constexpr std::size_t kCharacterFrameCount = 32;
inline constexpr std::size_t kCharacterFrameTileCount = 8;

// One tile of a frame: which 8x8 tile of the sprite sheet it shows, or nothing.
struct SheetTile {
    std::uint16_t raw;  // the tile's byte offset into the sheet, or kBlankRaw

    static constexpr std::uint16_t kTileBytes = 32;
    static constexpr std::uint16_t kBlankRaw = 0xFFFF;

    // The sheet's `tile`-th tile.
    static constexpr SheetTile at(int tile) { return {static_cast<std::uint16_t>(tile * kTileBytes)}; }
    static constexpr SheetTile blank() { return {kBlankRaw}; }

    constexpr bool isBlank() const { return raw == kBlankRaw; }
    // Which tile of the sheet. PRECONDITION: not blank.
    constexpr int tile() const { return raw / kTileBytes; }

    constexpr bool operator==(const SheetTile&) const = default;
};
static_assert(sizeof(SheetTile) == 2, "SheetTile must be byte-identical to a ROM word");

struct CharacterFrameEntry {
    std::uint8_t frame;
    std::array<SheetTile, kCharacterFrameTileCount> tiles;
};

// The eight tiles of `frame`. PRECONDITION (asserted): `frame` is below 32.
std::span<const SheetTile, kCharacterFrameTileCount> characterFrameTiles(int frame);

// All 32 frames in order.
std::span<const CharacterFrameEntry> characterFrames();

// --- a party member's poses -----------------------------------------------------------------------

// A pose is four steps, each a frame drawn as it is or mirrored left to right. A party member steps
// through them as its animation counter runs. Pose 0 shows nothing.
inline constexpr std::size_t kCharacterPoseCount = 39;
inline constexpr std::size_t kCharacterPoseSteps = 4;

// One step of a pose.
struct PoseFrame {
    std::uint8_t raw;  // the frame, the frame plus kMirroredBase, or kNoneRaw

    static constexpr std::uint8_t kMirroredBase = 0x30;
    static constexpr std::uint8_t kNoneRaw = 0xFF;

    static constexpr PoseFrame frame(int index) { return {static_cast<std::uint8_t>(index)}; }
    static constexpr PoseFrame mirrored(int index) {
        return {static_cast<std::uint8_t>(kMirroredBase + index)};
    }
    static constexpr PoseFrame none() { return {kNoneRaw}; }

    constexpr bool isNone() const { return raw == kNoneRaw; }
    constexpr bool isMirrored() const { return !isNone() && raw >= kMirroredBase; }
    // Which of the 32 frames. PRECONDITION: not none.
    constexpr int frameIndex() const { return isMirrored() ? raw - kMirroredBase : raw; }

    constexpr bool operator==(const PoseFrame&) const = default;
};
static_assert(sizeof(PoseFrame) == 1, "PoseFrame must be byte-identical to a ROM byte");

struct CharacterPoseEntry {
    std::uint8_t pose;
    std::array<PoseFrame, kCharacterPoseSteps> steps;
};

// The four steps of `pose`. PRECONDITION (asserted): `pose` is below 39.
std::span<const PoseFrame, kCharacterPoseSteps> characterPoseFrames(int pose);

// All 39 poses in order.
std::span<const CharacterPoseEntry> characterPoses();

// --- palettes ---------------------------------------------------------------------------------------

// The graphics slot after the last named one: the soldier's sprites, drawn green.
inline constexpr auto kGreenSoldierGraphics =
    static_cast<CharacterGfxId>(static_cast<std::uint8_t>(CharacterGfxId::GESTAHL) + 1);

inline constexpr std::size_t kBattleGraphicsCount =
    static_cast<std::size_t>(kGreenSoldierGraphics) + 1;

struct CharacterPaletteEntry {
    CharacterGfxId graphics;
    BattleCharacterPalette palette;
};

// The battle palette a party member with `graphics` is drawn in. PRECONDITION (asserted):
// `graphics` is a named graphics slot or kGreenSoldierGraphics.
BattleCharacterPalette battleCharacterPalette(CharacterGfxId graphics);

// All 24 slots in order.
std::span<const CharacterPaletteEntry> battleCharacterPalettes();

// A party member's palette is twelve colors from its battle palette followed by four more, one
// row of these per party slot: the colors its status effects, its cursor and its text draw in.
inline constexpr std::size_t kStatusColorCount = 4;

struct StatusColorEntry {
    std::uint8_t slot;
    std::array<CartridgeColor, kStatusColorCount> colors;
};

// The four colors party slot `slot`'s palette ends in. PRECONDITION (asserted): `slot` is below 4.
std::span<const CartridgeColor, kStatusColorCount> statusColors(std::size_t slot);

// All four rows in order.
std::span<const StatusColorEntry> statusColorRows();

// --- the party's sprite sheets ---------------------------------------------------------------------

// A party member's frames are cut from its field sprite sheet, one of the sheets the cartridge's
// MAP_SPRITE_GFX family holds end to end. Each graphics slot names where its sheet lies in the
// family and how long it is; the green soldier draws the soldier's sheet.
struct SheetPlace {
    std::uint32_t offset;  // bytes from the family's start
    std::uint32_t bytes;   // the sheet's own length

    constexpr bool operator==(const SheetPlace&) const = default;
};

struct CharacterSheetEntry {
    CharacterGfxId graphics;
    SheetPlace sheet;
};

// Where the sheet of a party member with `graphics` lies. PRECONDITION (asserted): `graphics` is
// a named graphics slot or kGreenSoldierGraphics.
SheetPlace characterSheet(CharacterGfxId graphics);

// All 24 slots in order.
std::span<const CharacterSheetEntry> characterSheets();

// --- where the party stands -------------------------------------------------------------------------

// The party stands along lines fanning out from a point above the screen. Each slot's line has an
// angle — 256 points straight down, each 128 is 45 degrees, larger values lean right — and the
// slot sits where its line crosses the slot's y.
inline constexpr std::size_t kPartySlotCount = 4;

// How many battle layouts there are: front, back, pincer and side, in that order.
inline constexpr std::size_t kBattleLayoutCount = 4;

struct PartySlotPlacement {
    std::uint8_t y;               // the slot's place down the screen
    std::uint16_t angle;          // the slot's line
    std::int16_t backRowAngle;    // added to the angle when the party member is in the back row
    bool mirrored;                // the party member faces right rather than left
    bool handsSwapped;            // the party member's weapon hands trade sides
};

struct PartyPlacementEntry {
    std::uint8_t layout;
    std::array<PartySlotPlacement, kPartySlotCount> slots;
};

// The four slots for a battle laid out as `layout`: 0 front, 1 back, 2 pincer, 3 side.
// PRECONDITION (asserted): `layout` is below 4.
std::span<const PartySlotPlacement, kPartySlotCount> partyPlacement(std::size_t layout);

// All four layouts in order.
std::span<const PartyPlacementEntry> partyPlacements();

// On the Magitek train's background each slot stands at its own y instead.
struct MagitekTrainPartyEntry {
    std::uint8_t slot;
    std::uint8_t y;
};

// The y of `slot` on the Magitek train. PRECONDITION (asserted): `slot` is below 4.
std::uint8_t magitekTrainPartyY(std::size_t slot);

// --- the pose a party member waits in -----------------------------------------------------------

// Each command has a pose its user takes while waiting to act. The table covers every command the
// battle can queue: the 30 named ones and eleven past them.
inline constexpr std::size_t kCommandWaitingPoseCount = 41;

struct CommandWaitingPoseEntry {
    BattleCommandId command;
    std::uint8_t pose;
};

// The commands past the named ones, by number.
struct UnnamedCommandWaitingPoseEntry {
    std::uint8_t command;
    std::uint8_t pose;
};

// The pose a party member waits in before `command`. PRECONDITION (asserted): `command` is below
// 41.
std::uint8_t commandWaitingPose(BattleCommandId command);

// --- damage numerals ------------------------------------------------------------------------------

// A damage numeral shows for 65 frames, lifted above where it stands by that frame's amount: up
// and back down, a smaller second bounce, then still.
inline constexpr std::size_t kDamageNumberFrames = 65;

struct DamageNumberBounceEntry {
    std::uint8_t frame;
    std::uint8_t lift;
};

// How many pixels the numeral stands raised on `frame`. PRECONDITION (asserted): `frame` is below
// 65.
std::uint8_t damageNumberLift(int frame);

// --- the gauge ------------------------------------------------------------------------------------

// The HUD draws a party member's gauge as four cells, each empty, full, or filled a pixel column at
// a time. A gauge's fill shifted right by 3 is its step; step n fills n + 1 columns from the left.
inline constexpr std::size_t kGaugeSteps = 32;
inline constexpr std::size_t kGaugeCells = 4;
inline constexpr std::uint8_t kGaugeCellFull = 8;

struct GaugeStepEntry {
    std::uint8_t step;
    std::array<std::uint8_t, kGaugeCells> cellFills;  // 0 (empty) to kGaugeCellFull per cell
};

// The four cells' fills at `step`. PRECONDITION (asserted): `step` is below 32.
std::span<const std::uint8_t, kGaugeCells> gaugeCellFills(int step);

// The small-font glyph of an empty gauge cell; a cell filled n columns is the glyph n past it.
struct GaugeEmptyGlyphEntry {
    Language language;
    std::uint8_t glyph;
};

std::uint8_t gaugeEmptyGlyph(Language language);

// --- messages -------------------------------------------------------------------------------------

// How many frames a battle message stays up at each of the eight message speeds, fastest first.
inline constexpr std::size_t kMessageSpeeds = 8;

struct MessageSpeedEntry {
    std::uint8_t setting;
    std::uint8_t frames;
};

// PRECONDITION (asserted): `setting` is below 8.
std::uint8_t messageHoldFrames(int setting);

// --- the colosseum ----------------------------------------------------------------------------------

// How far a colosseum monster stands below (or above, when negative) its ground line, by how the
// monster is aligned. A ceiling monster stands at the top of the screen instead, so its row is
// never read.
struct MonsterAlignOffsetEntry {
    MonsterVerticalAlignment alignment;
    std::int8_t yOffset;
};

// PRECONDITION (asserted): `alignment` is not CEILING.
std::int8_t colosseumAlignOffset(MonsterVerticalAlignment alignment);

// --- the numerals' glyphs -------------------------------------------------------------------------

// The 21 small-font glyphs the damage numerals are cut from, digits 0 to 9 first.
inline constexpr std::size_t kNumeralGlyphCount = 21;

struct NumeralGlyphEntry {
    std::uint8_t slot;
    std::uint8_t glyph;
};

std::span<const NumeralGlyphEntry> numeralGlyphs(Language language);

// --- the battle windows' tiles --------------------------------------------------------------------

// A battle window fills its middle with the wallpaper's 4 x 4 pattern and draws its border from
// the wallpaper's edge tiles, each part through a run of four tiles that repeats across or down.
inline constexpr std::size_t kBattleWindowPattern = 4;

struct BattleWindowWallpaperRow {
    std::uint8_t row;
    std::array<WindowTile, kBattleWindowPattern> tiles;
};

struct BattleWindowBorder {
    std::array<WindowTile, kBattleWindowPattern> top;
    std::array<WindowTile, kBattleWindowPattern> bottom;
    std::array<WindowTile, kBattleWindowPattern> corners;  // top-left, top-right, bottom-left, bottom-right
    std::array<WindowTile, kBattleWindowPattern> left;
    std::array<WindowTile, kBattleWindowPattern> right;
};

std::span<const BattleWindowWallpaperRow> battleWindowWallpaper();
const BattleWindowBorder& battleWindowBorder();

// --- the battle windows' places -------------------------------------------------------------------

// The tile layer a window or a run of HUD text sits on: the menu windows (the HUD's boxes among
// them), the menu text written over them, or the battlefield itself.
enum class BattleTileLayer : std::uint8_t {
    MENU_WINDOWS,
    MENU_TEXT,
    BATTLEFIELD,
};

// A window's place on its layer, in 8 x 8 cells, border included.
struct BattleWindowEntry {
    BattleWindowId window;
    BattleTileLayer layer;
    std::uint8_t column;
    std::uint8_t row;
    std::uint8_t width;
    std::uint8_t height;
};

inline constexpr std::size_t kBattleWindowCount = 15;

std::span<const BattleWindowEntry> battleWindows(Language language);
const BattleWindowEntry& battleWindow(BattleWindowId window, Language language);

// Where a run of HUD or menu text sits on its layer, in 8 x 8 cells. Each line of text takes two
// rows.
struct BattleTextPlacementEntry {
    BattleTextPlacementId placement;
    BattleTileLayer layer;
    std::uint8_t column;
    std::uint8_t row;
    std::uint8_t width;
    std::uint8_t height;
};

inline constexpr std::size_t kBattleTextPlacementCount = 19;

std::span<const BattleTextPlacementEntry> battleTextPlacements(Language language);
const BattleTextPlacementEntry& battleTextPlacement(BattleTextPlacementId placement,
                                                    Language language);

// --- the names in the narrow window ---------------------------------------------------------------

// The narrow window at the top of the screen shows one of six kinds of name. Each kind reads its
// name one of three ways: through a battle text command that splices a name by its index, from one
// of the two lists the text commands do not reach (esper attacks and Bushido techniques), or not
// at all.
inline constexpr std::size_t kNameKindCount = 6;

struct NameKindText {
    std::uint8_t raw;

    static constexpr std::uint8_t kSpecialList = 0x80;

    static constexpr NameKindText nothing() { return {0}; }
    static constexpr NameKindText splicedBy(BattleTextCommand command) {
        return {static_cast<std::uint8_t>(command)};
    }
    static constexpr NameKindText esperAttackName() { return {kSpecialList}; }
    static constexpr NameKindText bushidoName() { return {kSpecialList | 1}; }

    constexpr bool isNothing() const { return raw == 0; }
    constexpr bool isEsperAttackName() const { return raw == kSpecialList; }
    constexpr bool isBushidoName() const { return raw == (kSpecialList | 1); }
    // The text command that splices the name, for a kind that reads its name that way.
    constexpr std::optional<BattleTextCommand> command() const {
        if (isNothing() || (raw & kSpecialList) != 0) {
            return std::nullopt;
        }
        return static_cast<BattleTextCommand>(raw);
    }

    constexpr bool operator==(const NameKindText&) const = default;
};
static_assert(sizeof(NameKindText) == 1, "NameKindText must be byte-identical to a ROM byte");

struct NameKindEntry {
    std::uint8_t kind;
    NameKindText text;
};

// PRECONDITION (asserted): `kind` is below 6.
NameKindText nameKindText(int kind);

std::span<const NameKindEntry> nameKinds();

}  // namespace ostinato
