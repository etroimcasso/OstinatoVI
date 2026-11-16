// The cartridge's whole attack, and an attack on the attacker itself, run over a scenario and read
// back as named cells.

#include "oracle/damage_oracle.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/monster_id.h"

#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "oracle/status/attacker_effects_shared.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/attack_shared.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// The frame's own cells (ExecAttack, battle/battle_main.asm:8015-8139, and the routines it calls):
// the targeting byte and the command's extra steps in direct page; the targets of the attack being
// made, the MP it costs, the name it is shown under, the command whose hands each attack loads, the
// attack re-aimed at random (negative: it is not), the battler Interceptor guards, and the attack as
// it was chosen; the battlers facing right (party members in the low byte, monsters in the high);
// Strago's slot times two; the lores the party knows and the lores seen this battle, three bytes each.
constexpr std::uint32_t kTargetingByte = kWorkRam + 0x00BB;
constexpr std::uint32_t kExtraMultiplier = kWorkRam + 0x00BD;
constexpr std::uint32_t kCurrentTargets = kWorkRam + 0x3A30;
constexpr std::uint32_t kMpCost = kWorkRam + 0x3A4C;
constexpr std::uint32_t kNameKind = kWorkRam + 0x3412;
constexpr std::uint32_t kFightCommand = kWorkRam + 0x3413;
constexpr std::uint32_t kRandomizeTargets = kWorkRam + 0x3415;
constexpr std::uint32_t kInterceptorGuard = kWorkRam + 0x3416;
constexpr std::uint32_t kStragoSlot = kWorkRam + 0x3007;
constexpr std::uint32_t kLoreBytes = 3;
// The run difficulty, the byte after the monsters fallen (kMonstersFallen): read with it as a word.
constexpr std::uint32_t kRunDifficulty = kWorkRam + 0x3A3B;
constexpr std::uint32_t kSpecialAttackAnimation = kWorkRam + 0x3C81;
constexpr std::uint32_t kVigor = kWorkRam + 0x3B2C;
constexpr std::uint32_t kMagicPower = kWorkRam + 0x3B41;
constexpr std::uint32_t kHandHitRate = kWorkRam + 0x3B7C;
constexpr std::uint32_t kHandElement = kWorkRam + 0x3B90;
constexpr std::uint32_t kHandWeaponFlags = kWorkRam + 0x3BA4;
constexpr std::uint32_t kHandSpellCast = kWorkRam + 0x3D34;
constexpr std::uint32_t kHandBlockEffect = kWorkRam + 0x3CBC;
// The battle's lore list, one entry per lore: a known lore's number plus kLoreListFirstEntry, and
// 0xFF for a lore not known.
constexpr std::uint32_t kLoreList = kWorkRam + 0x310F;
constexpr std::uint8_t kLoreListFirstEntry = 0x37;

// The name kinds by the number ShowAttackNameTbl (battle_main.asm:8778-8787) gives them; the entry at
// 3 is a bare return that no command's start writes.
std::uint8_t nameKindByte(const std::optional<battle::AttackNameKind>& kind) {
    if (!kind) {
        return kNoOne;
    }
    switch (*kind) {
    case battle::AttackNameKind::Attack:
        return 0;
    case battle::AttackNameKind::Item:
        return 1;
    case battle::AttackNameKind::Esper:
        return 2;
    case battle::AttackNameKind::Bushido:
        return 4;
    case battle::AttackNameKind::Command:
        return 5;
    case battle::AttackNameKind::MonsterSpecialAttack:
        return 6;
    case battle::AttackNameKind::JokerDoom:
        return 7;
    case battle::AttackNameKind::Blitz:
        return 8;
    }
    return kNoOne;
}

// The script's commands as the frame writes them into the queue (_c2629e, battle_main.asm:16132-16149):
// four bytes each, the command first.
constexpr std::uint8_t kNameCommand = 0x01;
constexpr std::uint8_t kMonsterNameCommand = 0x11;
constexpr std::uint8_t kPoseResetCommand = 0x0E;

std::optional<battle::BattlerSlot> stragoInParty(const battle::BattleState& state) {
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        if (state.at(slot).actor == CharacterPropId::STRAGO) {
            return slot;
        }
    }
    return std::nullopt;
}

template <std::size_t N>
std::array<std::uint8_t, kLoreBytes> loreBytes(const std::bitset<N>& lores) {
    std::array<std::uint8_t, kLoreBytes> bytes{};
    for (std::size_t lore = 0; lore < N && lore < kLoreBytes * 8; ++lore) {
        if (lores.test(lore)) {
            bytes[lore / 8] = static_cast<std::uint8_t>(bytes[lore / 8] | 1u << (lore % 8));
        }
    }
    return bytes;
}

constexpr std::uint8_t kCriticalFlashBit = 0x20;
constexpr std::uint8_t kMonsterAttackBit = 0x01;
constexpr std::size_t kCoveredAllyByte = 8;
constexpr std::size_t kBlocksByte = 10;

battle::AttackAnimationParams frameParamsFromBytes(std::span<const std::uint8_t> block) {
    battle::AttackAnimationParams params = paramsFromBytes(block);
    params.criticalFlash = (block[0] & kCriticalFlashBit) != 0;
    params.monsterAttack = (block[0] & kMonsterAttackBit) != 0;
    if (block[kCoveredAllyByte] != 0) {
        params.coveredAlly = battle::BattlerSlot::fromIndex(block[kCoveredAllyByte] % battle::kBattlerSlots);
    }
    for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
        params.blocks[i] = static_cast<battle::BlockShown>(block[kBlocksByte + i]);
    }
    return params;
}
}  // namespace

void DamageOracle::writeAttackCells(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    // writeApplyCells() writes the to-work-out word whole; the Air Anchor byte in its high byte is
    // written after it.
    writeTargetEffectCells(scenario);

    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeInFlightByte(kTargetingByte, attack.targeting.bits);
    writeInFlightByte(kExtraMultiplier, static_cast<std::uint8_t>(attack.extraMultiplier));
    writeInFlightByte(kFirstAttackFlags, firstFlagsByte(attack));
    writeInFlightByte(kAttackFlags, secondFlagsByte(attack));
    writeInFlightWord(kCurrentTargets, maskOf(attack.scratch.currentTargets));
    // The frame loads, subtracts and clears the MP cost as a word; only that clear writes its high byte.
    writeInFlightWord(kMpCost, attack.scratch.mpCost);
    writeByte(kRunDifficulty, state.runDifficulty);
    writeInFlightByte(kNameKind, nameKindByte(attack.scratch.nameKind));
    writeInFlightByte(kFightCommand, attack.scratch.fightCommand ? static_cast<std::uint8_t>(*attack.scratch.fightCommand) : kNoOne);
    writeInFlightByte(kRandomizeTargets, attack.scratch.randomizeTargets ? 0 : kNoOne);
    writeByte(kInterceptorGuard, slotByte(state.scratch.interceptorGuarding));
    writeInFlightByte(kQueuedAttack, static_cast<std::uint8_t>(attack.queuedAttack));
    writeWord(kFacingRight, maskOf(state.facingRight));
    writeByte(kStragoSlot, slotByte(stragoInParty(state)));
    writeBytes(retropp::MemoryRegion{.at = kLoresKnown, .size = kLoreBytes}, loreBytes(state.party.loresKnown));
    writeBytes(retropp::MemoryRegion{.at = kLoresLearned, .size = kLoreBytes}, loreBytes(state.loresLearned));
    for (std::size_t lore = 0; lore < game::kLores; ++lore) {
        writeByte(kLoreList + static_cast<std::uint32_t>(lore),
                  state.party.loresKnown.test(lore) ? static_cast<std::uint8_t>(kLoreListFirstEntry + lore) : kNoOne);
    }
    // The sketch cell holds the sketcher while it makes the attack its sketch caught.
    std::optional<battle::BattlerSlot> sketched = attack.scratch.sketcher;
    if (!sketched && attack.scratch.sketch) {
        sketched = attack.scratch.sketch->monster;
    }
    writeInFlightByte(kSketched, slotByte(sketched));
    // The script starts empty: the frame writes every command and params block of its own.
    writeInFlightByte(kQueueEnd, 0);
    writeInFlightByte(kAnimationPlace, 0);
    writeInFlightByte(kNextParamsBlock, 0);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        std::uint8_t triggers = battler.triggers.retort ? kRetortBit : 0;
        triggers |= battler.triggers.monsterRunic ? kMonsterRunicBit : 0;
        triggers |= battler.triggers.runic ? kRunicBit : 0;
        writeByte(kTriggers + at, triggers);
        writeByte(kSpecialAttack + at, battler.specialAttack.packed);
        writeByte(kSpecialAttackAnimation + at, static_cast<std::uint8_t>(battler.specialAttackAnimation));
        writeWord(kNamedMonster + at, static_cast<std::uint16_t>(battler.monster.value_or(battler.rage.value_or(MonsterId{}))));
        writeByte(kVigor + at, battler.vigor);
        writeByte(kMagicPower + at, battler.magicPower);
        for (const auto& [hand, place] : {std::pair{&battler.weaponHand, at}, std::pair{&battler.shieldHand, at + 1}}) {
            writeByte(kHandPower + place, hand->battlePower);
            writeByte(kHandItems + place, static_cast<std::uint8_t>(hand->item));
            writeByte(kHandHitRate + place, hand->hitRate);
            writeByte(kHandElement + place, hand->element.bits);
            writeByte(kHandWeaponFlags + place, hand->weaponFlags.bits);
            writeByte(kHandSpellCast + place, hand->spellCast.raw);
            writeByte(kHandBlockEffect + place, hand->blockEffect.packed);
        }
    }
}

AttackResult DamageOracle::attack(const DamageScenario& scenario) {
    writeAttackCells(scenario);
    call([&] { execAttack_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readAttackResult(true);
}

AttackResult DamageOracle::selfAttack(const DamageScenario& scenario) {
    writeAttackCells(scenario);
    call([&] { execSelfAttack_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readAttackResult(true);
}

AttackResult DamageOracle::readAttackResult(bool attackCells) {
    EffectCells out;
    putApplied(out, readApplyCells());
    putStatusChanges(out, readStatusChangesResult());
    putEffectCells(out, readTargetEffectResult().cells);
    out.put("sketched", slotText(slotTimesTwo(readByte(kSketched))));
    out.put("flipped", std::to_string(readWord(kFlippedSet)));

    // The whole queue, each animation, bounce and Launcher's hits with the next params block.
    std::string script;
    const std::uint8_t end = readByte(kQueueEnd);
    const std::vector<std::uint8_t> queue = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = end});
    const int blockCount = readByte(kNextParamsBlock) / kParamsBlockSize;
    int nextBlock = 0;
    const auto takeBlock = [&]() -> std::optional<std::vector<std::uint8_t>> {
        if (nextBlock >= blockCount) {
            return std::nullopt;
        }
        return machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(nextBlock++), .size = kParamsBlockSize});
    };
    std::uint32_t buffers = 0;
    for (std::size_t at = 0; at + kQueueCommandSize <= queue.size(); at += kQueueCommandSize) {
        const std::span<const std::uint8_t> entry{queue.data() + at, kQueueCommandSize};
        const auto word = static_cast<std::uint16_t>(entry[2] | entry[3] << 8);
        const auto numeral = [](battle::BattlerSlot slot, std::uint16_t value) {
            return battle::DamageNumber{.target = slot,
                                        .amount = static_cast<std::uint16_t>(value & kAmountBits),
                                        .miss = (value & kMissMark) != 0,
                                        .heals = (value & kHealMark) != 0};
        };
        switch (entry[0]) {
        case kShowAnimationCommand: {
            script += "animation " + std::to_string(entry[1]);
            if (animationReadsItsAttack(entry[1])) {
                script += " " + std::to_string(entry[2]) + " " + std::to_string(entry[3]);
            }
            const auto block = takeBlock();
            script += block ? " | " + frameParamsText(frameParamsFromBytes(*block)) : std::string{" | no params"};
            break;
        }
        case kReflectedCommand:
        case kScatteredCommand: {
            const auto block = takeBlock();
            script += block ? bounceText(entry, *block) : "bounce " + std::to_string(entry[0]) + " | no params";
            break;
        }
        case kNameCommand:
            script += "name " + std::to_string(entry[1]) + " " + std::to_string(entry[2]);
            break;
        case kMonsterNameCommand:
            script += "monster name " + std::to_string(entry[1] | entry[2] << 8);
            break;
        case kMessageCommand:
            script += "message " + std::to_string(entry[1]);
            break;
        case kPoseResetCommand:
            script += "pose reset " + std::to_string(entry[1]);
            break;
        case kOneNumeral:
            script += "numeral " + numeralText(numeral(battle::BattlerSlot::fromIndex(entry[1] % battle::kBattlerSlots), word));
            break;
        case kEveryNumeral: {
            script += "numerals together";
            const std::uint32_t buffer = kNumeralBuffer + kNumeralBufferSize * buffers++;
            for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
                const std::uint16_t value = readWord(buffer + static_cast<std::uint32_t>(index * 2));
                if (value != kNoDamage) {
                    script += " " + numeralText(numeral(battle::BattlerSlot::fromIndex(index), value));
                }
            }
            break;
        }
        default:
            script += "command " + std::to_string(entry[0]) + " " + std::to_string(entry[1]) + " " +
                      std::to_string(entry[2]) + " " + std::to_string(entry[3]);
            break;
        }
        script += "; ";
    }
    while (const auto block = takeBlock()) {
        script += "params with no animation | " + frameParamsText(frameParamsFromBytes(*block)) + "; ";
    }
    out.put("script", script);

    putNextAttack(out, readByte(kFirstAttackFlags), readByte(kAttackFlags));
    out.put("attacks left", readByte(kAttacksLeft));
    out.put("damage modification", readByte(kDamageModification) != 0);
    out.put("re-aimed at random", (readByte(kRandomizeTargets) & kTopBit) == 0);
    out.put("quiet misses", readByte(kQuietMisses) == 0);
    out.put("away from the field unreachable", (readByte(kReachKeepsAway) & kTopBit) != 0);
    out.put("Interceptor guarding", slotText(slotTimesTwo(readByte(kInterceptorGuard))));
    if (!attackCells) {
        std::erase_if(out.cells, [](const auto& cell) { return attackOwnCell(cell.first); });
    }
    return AttackResult{.cells = std::move(out.cells)};
}

}  // namespace ostinato::trace
