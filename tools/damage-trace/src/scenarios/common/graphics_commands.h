// The commands the battle program asks the graphics program for (ExecBtlGfx, battle/battle_main.asm
// :16369-16387; BtlGfxTbl, btlgfx/btlgfx_main.asm:87-101), by the number the battle program passes, in
// namespace graphics inside ostinato::trace. What each one is for the port is
// docs/contracts/battle_core.md § What the graphics program hands the battle program.
#pragma once

#include <cstdint>

namespace ostinato::trace::graphics {

inline constexpr std::uint8_t kOpenBattleScene = 0x00;    // BtlGfx_00, the battle's opening
inline constexpr std::uint8_t kOneFrame = 0x01;           // BtlGfx_01, wait one frame
inline constexpr std::uint8_t kOpenMenu = 0x02;           // a party member's battle menu opens
inline constexpr std::uint8_t kCloseMenu = 0x03;          // a battle menu closes
inline constexpr std::uint8_t kPlayBattleScript = 0x04;   // the battle script plays
inline constexpr std::uint8_t kAddItemObtained = 0x05;    // an item joins the items obtained
inline constexpr std::uint8_t kRefreshMonsterNames = 0x06;
inline constexpr std::uint8_t kVictory = 0x08;            // the celebration (WinBattle :15294)
inline constexpr std::uint8_t kCloseBattle = 0x09;        // TerminateBattle :150-152, the battle's last
inline constexpr std::uint8_t kMergeItemsObtained = 0x0A; // the items obtained join the inventory
inline constexpr std::uint8_t kRefreshMenu = 0x0B;        // a battle menu's commands are redrawn
inline constexpr std::uint8_t kScrollFinalBattle = 0x0C;  // BattleEnd_06 :12235
inline constexpr std::uint8_t kMenuQueuePosition = 0x0D;  // never asked for by the battle program

// The slot byte of kOpenMenu (_c211ef battle/battle_main.asm:2796-2804): the party slot, with this bit
// set for the menu of the monster the party member controls.
inline constexpr std::uint8_t kControllingMonsterBit = 0x80;

}  // namespace ostinato::trace::graphics
