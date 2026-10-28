// A party member's battle sprites, cut from its field sprite sheet in the player's cartridge.
//
// The battle draws a party member in one of 32 frames (data/battle_gfx_tables.h, characterFrameTiles),
// each eight tiles of the character's map sprite sheet. The sheet is one of the MAP_SPRITE_GFX
// family's items (characterSheet). The frames are cut here into two sheets ready to upload: the 31
// standing frames, each 16 x 32 with its top row blank so 16 x 24 shows, and the fallen frame, 32 x
// 16. Two of the standing frames have their leg tiles mirrored in the cut, as the loader mirrors
// them.
//
// A party member draws through sixteen colors: twelve from its battle palette and the four status
// colors of its party slot.
//
//     const CharacterBattleSheet sheet = content.characterSheets.sheet(CharacterGfxId::TERRA);
//     sheet.standing;   // 16 x (32 * 32): frame f in rows 32f to 32f + 31
//     sheet.fallen;     // 32 x 16
//     characterColors(content.characterSheets, CharacterGfxId::TERRA, /*partySlot=*/0);
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "data/cartridge_color.h"
#include "ostinato/character_gfx_id.h"
#include "text/font.h"

namespace ostinato {

// A standing frame's cell and the fallen frame's.
inline constexpr int kCharacterFrameWidthPx = 16;
inline constexpr int kCharacterFrameHeightPx = 32;
inline constexpr int kCharacterFrameShownRows = 24;  // the top tile row of a standing frame is blank
inline constexpr int kFallenFrameWidthPx = 32;
inline constexpr int kFallenFrameHeightPx = 16;

// A party member's twelve palette colors, and the sixteen it draws through with the status colors.
inline constexpr std::size_t kCharacterPaletteColors = 12;
inline constexpr std::size_t kCharacterColorCount = 16;

// The two frames whose leg tiles the loader mirrors as it cuts them: the frames' two bottom tiles
// are each mirrored in place.
inline constexpr std::array<int, 2> kFramesWithMirroredLegs{3, 16};

struct CharacterBattleSheet {
    text::IndexedImage standing;  // 32 cells of 16 x 32, one under another; cell 0 is blank
    text::IndexedImage fallen;    // one cell of 32 x 16

    bool valid() const { return standing.width > 0; }
};

// Every party member's sheets and colors, over the cartridge regions holding the map sprite sheets
// and the battle palettes. The bytes must outlive this object.
class CharacterBattleSheets {
public:
    CharacterBattleSheets() = default;

    // `spriteSheets` is MAP_SPRITE_GFX and `palettes` BATTLE_CHAR_PAL.
    CharacterBattleSheets(std::span<const std::uint8_t> spriteSheets,
                          std::span<const std::uint8_t> palettes);

    bool valid() const;

    // The sheets for a party member drawn with `graphics`. Throws std::out_of_range when the
    // frames read past the sprite sheets.
    CharacterBattleSheet sheet(CharacterGfxId graphics) const;

    // The twelve colors of `graphics`' battle palette. `greenSoldier` draws the soldier's sprites
    // in the green palette instead. Throws std::out_of_range when the palette is not in the bytes.
    std::array<CartridgeColor, kCharacterPaletteColors> paletteColors(CharacterGfxId graphics,
                                                                      bool greenSoldier = false) const;

private:
    std::span<const std::uint8_t> spriteSheets_;
    std::span<const std::uint8_t> palettes_;
};

// The sixteen colors a party member in `partySlot` draws through: its battle palette's twelve and
// the slot's four status colors.
std::array<CartridgeColor, kCharacterColorCount> characterColors(const CharacterBattleSheets& sheets,
                                                                 CharacterGfxId graphics,
                                                                 std::size_t partySlot,
                                                                 bool greenSoldier = false);

}  // namespace ostinato
