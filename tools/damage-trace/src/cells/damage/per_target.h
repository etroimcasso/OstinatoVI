// The work-RAM cells the damage routines read and write: the per-target pass, the damage taken and
// shown, and the reflected attack's aim and hit check. The work-RAM base and the relic-effect cells
// are defined here only; DamageOracle's members of the same names are these.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/character_record.h"

namespace ostinato::trace::cells {

inline constexpr std::uint32_t kWorkRam = 0x7E0000;
inline constexpr std::uint32_t kRelicEffects1And3 = kWorkRam + 0x3C44;
inline constexpr std::uint32_t kRelicEffects4And5 = kWorkRam + 0x3C58;
inline constexpr std::uint32_t kEffect = kWorkRam + 0x11A9;            // the special effect, its index doubled
inline constexpr std::uint32_t kFirstAttackFlags = kWorkRam + 0x00B2;  // bit 7: a drain is capped
inline constexpr std::uint32_t kAnimationCommand = kWorkRam + 0x00B4;  // the script command the animation goes out as
// Every command starts with its animation going out as script command $06, show the animation
// (ExecCmd, battle_main.asm:3096-3097).
inline constexpr std::uint8_t kShowAnimationCommand = 0x06;
inline constexpr std::uint32_t kCommand = kWorkRam + 0x00B5;
inline constexpr std::uint32_t kAttackIndex = kWorkRam + 0x00B6;
inline constexpr std::uint32_t kSecondary = kWorkRam + 0x00B7;
inline constexpr std::uint32_t kRngPosition = kWorkRam + 0x00BE;
inline constexpr std::uint32_t kDamageModification = kWorkRam + 0x3414;
inline constexpr std::uint32_t kGolemBlock = kWorkRam + 0x3A82;
inline constexpr std::uint32_t kInterceptorBlock = kWorkRam + 0x3A83;
inline constexpr std::uint32_t kForcefield = kWorkRam + 0x3EC8;
// The battle script's queue: where the attack's animation went, the next free place, the queue.
inline constexpr std::uint32_t kAnimationPlace = kWorkRam + 0x3A71;
inline constexpr std::uint32_t kQueueEnd = kWorkRam + 0x3A72;
inline constexpr std::uint32_t kQueue = kWorkRam + 0x2D6E;
inline constexpr std::uint8_t kQueueCommandSize = 4;
// Per battler, at twice the battler's index.
inline constexpr std::uint32_t kLevel = kWorkRam + 0x3B18;
inline constexpr std::uint32_t kDefenses = kWorkRam + 0x3BB8;   // defense, then magic defense
inline constexpr std::uint32_t kHp = kWorkRam + 0x3BF4;
inline constexpr std::uint32_t kMp = kWorkRam + 0x3C08;
inline constexpr std::uint32_t kMaxHp = kWorkRam + 0x3C1C;
inline constexpr std::uint32_t kMaxMp = kWorkRam + 0x3C30;
inline constexpr std::uint32_t kAbsorbedNullified = kWorkRam + 0x3BCC;
inline constexpr std::uint32_t kWeakHalved = kWorkRam + 0x3BE0;
inline constexpr std::uint32_t kMonsterFlags = kWorkRam + 0x3C95;
inline constexpr std::uint32_t kStance = kWorkRam + 0x3AA1;
inline constexpr std::uint32_t kStatus12 = kWorkRam + 0x3EE4;
inline constexpr std::uint32_t kStatus34 = kWorkRam + 0x3EF8;
inline constexpr std::uint32_t kToSet12 = kWorkRam + 0x3DD4;
inline constexpr std::uint32_t kToSet34 = kWorkRam + 0x3DE8;
inline constexpr std::uint32_t kToClear12 = kWorkRam + 0x3DFC;
inline constexpr std::uint32_t kToClear34 = kWorkRam + 0x3E10;
inline constexpr std::uint32_t kController = kWorkRam + 0x32B9;
inline constexpr std::uint32_t kLastAttacker = kWorkRam + 0x32E0;
inline constexpr std::uint32_t kTaken = kWorkRam + 0x33D0;
inline constexpr std::uint32_t kHealed = kWorkRam + 0x33E4;

// Per battler, a word at twice the battler's index: its bit in the battler sets below (the party
// members' in the low byte, the monsters' in the high byte), what is to be worked out again for it,
// and the battler that last damaged it.
inline constexpr std::uint32_t kBattlerBit = kWorkRam + 0x3018;
inline constexpr std::uint32_t kToUpdate = kWorkRam + 0x3204;
inline constexpr std::uint32_t kLastDamagedBy = kWorkRam + 0x327C;
inline constexpr std::uint32_t kMissed = kWorkRam + 0x3A5A;
inline constexpr std::uint32_t kInvincible = kWorkRam + 0x3A3C;
inline constexpr std::uint32_t kGolemHp = kWorkRam + 0x3A36;
inline constexpr std::uint32_t kWeaponSpellcast = kWorkRam + 0x3A89;
inline constexpr std::uint32_t kBlackBeltTargets = kWorkRam + 0x3419;  // a party member's bit clear: it may be countered for
// The numerals for five or more battlers are copied to the graphics buffer, 20 bytes per command,
// counted from the buffer's start.
inline constexpr std::uint32_t kNumeralBuffer = kWorkRam + 0x2BCE;
inline constexpr std::uint32_t kNumeralBufferCount = kWorkRam + 0x3A34;
inline constexpr std::uint32_t kNumeralBufferSize = 20;
// The castable spells to be worked out again.
inline constexpr std::uint8_t kEnabledMagicBit = 0x80;
// The numeral commands: one battler's numeral, or every battler's at once from the buffer.
inline constexpr std::uint8_t kOneNumeral = 0x0B;
inline constexpr std::uint8_t kEveryNumeral = 0x03;
// A numeral's word: the gain mark, the miss mark and the amount.
inline constexpr std::uint16_t kHealMark = 0x8000;
inline constexpr std::uint16_t kMissMark = 0x4000;
inline constexpr std::uint16_t kAmountBits = 0x3FFF;
inline constexpr std::uint8_t kPartyBits = 0x0F;

inline constexpr std::uint32_t kTargets = kWorkRam + 0x00B8;
inline constexpr std::uint32_t kAimingRules = kWorkRam + 0x00BA;
inline constexpr std::uint32_t kAimFlags = kWorkRam + 0x3A46;          // bit 1: the next aim is exact
inline constexpr std::uint32_t kEarlierTargets = kWorkRam + 0x3A4E;
inline constexpr std::uint32_t kEnemyCharacters = kWorkRam + 0x3A40;   // party members fighting against the party
inline constexpr std::uint32_t kBattleType = kWorkRam + 0x201F;
inline constexpr std::uint32_t kPincerFirstSide = kWorkRam + 0x2EAC;
inline constexpr std::uint32_t kPincerSecondSide = kWorkRam + 0x2EAD;
// The reach check ANDs four cells: the party members' bits of the first two words with the
// monsters' bits of a byte and the next two words.
inline constexpr std::uint32_t kReachMonsters = kWorkRam + 0x2F46;
inline constexpr std::uint32_t kReachCharacters = kWorkRam + 0x3403;
inline constexpr std::uint32_t kReachFirst = kWorkRam + 0x3A78;
inline constexpr std::uint32_t kReachSecond = kWorkRam + 0x3408;
inline constexpr std::uint32_t kCountersEnabled = kWorkRam + 0x341A;   // negative: counterattacks are on
// The reach check loads the counterattack cell as a word, so its sign is this next cell's: negative,
// battlers away from the field are dropped. A command starts it at $FF and only an attack aimed at
// its own attacker clears it.
inline constexpr std::uint32_t kReachKeepsAway = kWorkRam + 0x341B;
inline constexpr std::uint32_t kAwayFromTheField = kWorkRam + 0x3F2C;
inline constexpr std::uint32_t kTargetCount = kWorkRam + 0x3EC9;
inline constexpr std::uint32_t kQuietMisses = kWorkRam + 0x341C;       // 0: misses show nothing
inline constexpr std::uint32_t kHitInTheBack = kWorkRam + 0x3A54;
inline constexpr std::uint32_t kQueuedCommand = kWorkRam + 0x3A7C;
inline constexpr std::uint32_t kLastSpell = kWorkRam + 0x3410;
inline constexpr std::uint32_t kLastItem = kWorkRam + 0x3411;
inline constexpr std::uint32_t kExtraHits = kWorkRam + 0x3405;         // one less than the hits still to come
// The params blocks the script's animations read, sixteen bytes each, and where the next goes; the
// battle-type step reads the attacker of the block before it.
inline constexpr std::uint32_t kParamsBlocks = kWorkRam + 0x2C6E;
inline constexpr std::uint32_t kNextParamsBlock = kWorkRam + 0x3A32;
inline constexpr std::uint8_t kParamsBlockSize = 16;
// Per battler, at twice the battler's index.
inline constexpr std::uint32_t kOnTheField = kWorkRam + 0x3AA0;        // bit 0
inline constexpr std::uint32_t kCharmedBy = kWorkRam + 0x3395;
inline constexpr std::uint32_t kStamina = kWorkRam + 0x3B40;
inline constexpr std::uint32_t kEvadeAndMagicBlock = kWorkRam + 0x3B54;  // evade, then magic block
inline constexpr std::uint32_t kBlockGraphics = kWorkRam + 0x3CE4;       // a blow's, then a spell's
inline constexpr std::uint32_t kRetort = kWorkRam + 0x3E4C;              // bit 0
inline constexpr std::uint32_t kLastHitCommandBy = kWorkRam + 0x3290;
inline constexpr std::uint32_t kLastHitCommand = kWorkRam + 0x3D48;
inline constexpr std::uint32_t kLastHitSpellBy = kWorkRam + 0x3291;
inline constexpr std::uint32_t kLastHitSpell = kWorkRam + 0x3D49;
inline constexpr std::uint32_t kLastHitItemBy = kWorkRam + 0x32A4;
inline constexpr std::uint32_t kLastHitItem = kWorkRam + 0x3D5C;
inline constexpr std::uint32_t kLastHitElementBy = kWorkRam + 0x32A5;
inline constexpr std::uint32_t kLastHitElement = kWorkRam + 0x3D5D;

inline constexpr std::uint8_t kAimExactlyBit = 0x02;
inline constexpr std::uint8_t kCannotBeCriticalBit = 0x02;
inline constexpr std::uint8_t kVanishAppliesBit = 0x80;
inline constexpr std::uint8_t kInstantDeathProtectedBit = 0x04;
inline constexpr std::uint8_t kCountersOn = 0x80;
inline constexpr std::uint8_t kAwayUnreachable = 0xFF;  // kReachKeepsAway as a command starts
inline constexpr std::uint8_t kNoHitsToCome = 0xFF;
// The bounce's two commands.
inline constexpr std::uint8_t kReflectedCommand = 0x09;
inline constexpr std::uint8_t kScatteredCommand = 0x15;
inline constexpr std::size_t kBlockSet = 14;
inline constexpr std::uint8_t kNoOne = 0xFF;
inline constexpr std::uint16_t kNoDamage = 0xFFFF;
inline constexpr std::uint8_t kDrainCappedBit = 0x80;
inline constexpr std::uint8_t kWaitingBit = 0x80;
// The monster flags byte: undead, the Imp critical, human, the name hidden, dying at 0 MP.
inline constexpr std::uint8_t kUndeadBit = 0x80;
inline constexpr std::uint8_t kImpCriticalBit = 0x40;
inline constexpr std::uint8_t kHumanBit = 0x10;
inline constexpr std::uint8_t kNameHiddenBit = 0x04;
inline constexpr std::uint8_t kDiesAtZeroMpBit = 0x01;
// The stance byte: Defending, the back row, a damage-over-time action waiting, the actions going
// back in line when the battler can act again, and the next action skipping the advance wait.
inline constexpr std::uint8_t kDefendingBit = 0x02;
inline constexpr std::uint8_t kBackRowBit = 0x20;
inline constexpr std::uint8_t kDamageOverTimeWaitingBit = 0x10;
inline constexpr std::uint8_t kRequeuePendingBit = 0x80;
inline constexpr std::uint8_t kSkipAdvanceWaitBit = 0x01;
// The queue's place for the attack's animation is filled with this before a call, and the port's
// with an animation of this second value, so a rewrite on either side shows.
inline constexpr std::uint8_t kUnwritten = 0xEE;

// The second attack-flags byte as a command starts it, every bit set, with the two bits the damage
// routines read: set while the hand is not held in both hands, set while an attack that ignores
// defense is not multiplied (docs/contracts/battle_commands.md, "A command's start").
inline constexpr std::uint8_t kAttackFlagsAtStart = 0xFF;
inline constexpr std::uint8_t kNotTwoHanded = 0x40;
inline constexpr std::uint8_t kIgnoreDefenseStopsMultiplier = 0x10;

// The first relic word holds relic effects 1 in its low byte, with that byte's top bit taken by
// relic effects 2's top bit, and relic effects 3 in its high byte (InitCharBattle).
inline constexpr std::uint8_t kLowSevenBits = 0x7F;
inline constexpr std::uint8_t kTopBit = 0x80;

}  // namespace ostinato::trace::cells
