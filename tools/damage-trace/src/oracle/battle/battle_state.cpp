// The battle whole on the oracle: a BattleState laid into the cartridge's cells and compared with
// them by the bits that have meaning, and the gates that keep the comparison whole — every cell the
// battle program writes compared or listed, and every listed cell it reads written first.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/party_setup.h"
#include "data/formations.h"
#include "data/monster_properties.h"
#include "game/character_record.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"

#include "cells/ai/ai.h"
#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/ending/ending.h"
#include "cells/ending/party.h"
#include "cells/ending/records.h"
#include "cells/status/effects.h"
#include "cells/battle/state.h"
#include "cells/status/status.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// What the start's fill leaves in a cell of $2000-$341F (InitBattle :6063-6067).
constexpr std::uint8_t kFilled = 0xFF;
// A party member's rage, low byte, while no rage is chosen: the fill's.
constexpr std::uint8_t kNoRageChosen = kFilled;
// An empty inventory record as LoadItemProp leaves it (:13987-13994): no item, unusable, the targeting
// of the record loaded before it, the quantity, and no hand that can hold it.
constexpr std::uint8_t kUnusableItem = 0x80;
constexpr std::uint8_t kNoHandHoldsIt = 0xFF;
constexpr std::size_t kRecordTargeting = 2;
// The empty party slot's record offset, left by the fill (InitParty :7848-7850 skips the slot).
constexpr std::uint16_t kNoRecord = 0xFFFF;
// LoadBattleProp toggles the top four bits of the formation's first byte as it copies it (:7975-7977).
constexpr std::uint8_t kFormationFirstByteToggled = 0xF0;
// $3205,x: bit 7 the battler may act after a Jump (set :264-266, cleared :1797-1799, read :2789, :5758);
// bit 2 Air Anchor is not on it (:1129-1131, :9024-9028, :9743-9745); bit 1 its near-fatal spell has not
// been cast this battle (:1626-1629).
constexpr std::uint8_t kMayActAfterJumpBit = 0x80;
constexpr std::uint8_t kNearFatalSpellAvailableBit = 0x02;
// $3AA0,x, every bit a field: 0 on the field, 1 its commands come from the menu (:1437), 2 its menu is
// open, 3 its gauge is stopped, 4 a status holds it, 5 its action is queued (:2747), 6 control holds it
// (:1386-1409), 7 its menu may open (:1441).
constexpr std::uint8_t kTakesMenuInputBit = 0x02;
constexpr std::uint8_t kAtbStoppedBit = 0x08;
constexpr std::uint8_t kActionQueuedBit = 0x20;
constexpr std::uint8_t kHeldByControlFieldBit = 0x40;
// $B1's bits beyond the counterattack, the run (kCounterattackBit, kCannotRunBit, kEscapeBlockedBit) and
// the menus (kMenusClosedBit).
constexpr std::uint8_t kBackAttackFlagBit = 0x20;
constexpr std::uint8_t kMimickingBit = 0x40;
// A party slot's place in the monsters' index list (kMonsterIndex), $1FF9-$2000: no monster stands there.
constexpr std::uint32_t kPartySlotsMonsterIndexBytes = 8;
// The tables of two bytes a battler the start loads from a party member's record (LoadCharProp
// :6602-6661, UpdateEquipBattle :6745-6828): the to-update flags, HP, MP and their maxima, the statuses
// to set, the level and speed, vigor, stamina and magic power, evade and magic block, the equipment's
// statuses, the run factor, the monster flags' undead and human bits, the relic effects, the hands'
// power, hit rate, spell, element and effects, defense, the statuses blocked, the elements, the block
// effects and graphics, the hands' items and the relics; and the gauge's start and its rate, worked
// from the speed (InitGauge :6228-6303, CalcSpeed :1539-1578).
constexpr std::array<std::uint32_t, 33> kLoadedThroughTheRecord{
    kWorkRam + 0x3204, kWorkRam + 0x3BF4, kWorkRam + 0x3C08, kWorkRam + 0x3C1C, kWorkRam + 0x3C30,
    kWorkRam + 0x3DD4, kWorkRam + 0x3DE8, kWorkRam + 0x3B18, kWorkRam + 0x3B2C, kWorkRam + 0x3B40,
    kWorkRam + 0x3B54, kWorkRam + 0x3C6C, kWorkRam + 0x3D70, kWorkRam + 0x3C94, kWorkRam + 0x3C44,
    kWorkRam + 0x3B68, kWorkRam + 0x3B7C, kWorkRam + 0x3D34, kWorkRam + 0x3B90, kWorkRam + 0x3BA4,
    kWorkRam + 0x3BB8, kWorkRam + 0x331C, kWorkRam + 0x3330, kWorkRam + 0x3BCC, kWorkRam + 0x3BE0,
    kWorkRam + 0x3CBC, kWorkRam + 0x3CA8, kWorkRam + 0x3CD0, kWorkRam + 0x3CE4, kWorkRam + 0x3C58,
    kWorkRam + 0x3EE4, kWorkRam + 0x3218, kWorkRam + 0x3AC8};

// A boost as a record's maximum HP or MP word holds it in its top two bits (MaxHPMPTbl :6708-6712):
// 0 none, 1 a quarter more, 2 a half more, 3 an eighth more.
std::uint16_t boostBits(battle::MaxBoost boost) {
    switch (boost) {
        case battle::MaxBoost::None:
            return 0;
        case battle::MaxBoost::Plus25Percent:
            return 1;
        case battle::MaxBoost::Plus50Percent:
            return 2;
        case battle::MaxBoost::Plus12Point5Percent:
            return 3;
    }
    return 0;
}
}  // namespace

void DamageOracle::layAsTheCartridgeHoldsIt(std::uint32_t at, std::span<const std::uint8_t> bytes) {
    writeCells(retropp::MemoryRegion{.at = at, .size = static_cast<std::uint32_t>(bytes.size())}, bytes,
               CellWrite::AsTheCartridgeHoldsIt);
}

void DamageOracle::layByteAsTheCartridgeHoldsIt(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    layAsTheCartridgeHoldsIt(at, bytes);
}

void DamageOracle::layWordAsTheCartridgeHoldsIt(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>(value >> 8)};
    layAsTheCartridgeHoldsIt(at, bytes);
}

void DamageOracle::leaveOut(std::uint32_t at, std::uint32_t count) {
    if (capture_ == nullptr) {
        return;
    }
    for (std::uint32_t cell = at; cell < at + count; ++cell) {
        capture_->cells.erase(cell);
        capture_->disagreements.erase(cell);
        capture_->leftOut.insert(cell);
    }
}

void DamageOracle::leaveOutBits(std::uint32_t at, std::uint8_t bits) {
    if (capture_ != nullptr) {
        capture_->bitsLeftOut[at] |= bits;
    }
}

std::uint8_t DamageOracle::comparedBits(const LaidState& laid, std::uint32_t cell) {
    const auto leftOut = laid.bitsLeftOut.find(cell);
    return static_cast<std::uint8_t>(meaningBits(cell) & ~(leftOut == laid.bitsLeftOut.end() ? 0 : leftOut->second));
}

void DamageOracle::leaveOutAsCarriedToNothing(std::uint32_t at, std::uint32_t count) {
    leaveOut(at, count);
    if (capture_ == nullptr) {
        return;
    }
    for (std::uint32_t cell = at; cell < at + count; ++cell) {
        capture_->carriedToNothing.insert(cell);
    }
}

void DamageOracle::writeStateAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    // The hands, then the inventory, in the order InitInventory loads them (:13914-13941): each shield
    // and each weapon last party slot first, then the items from the last slot down. An empty record,
    // a hand's or the inventory's, keeps the targeting the record loaded before it left; a hand read
    // through an empty party slot's offset (bank $7F, kCellsNotCompared) leaves it unknown.
    std::optional<std::uint8_t> targeting = kFilled;
    for (const auto* hands : {&state.inventory.shieldHands, &state.inventory.weaponHands}) {
        const std::uint32_t first = hands == &state.inventory.shieldHands ? kShieldHands : kWeaponHands;
        for (int position = battle::kPartySlots - 1; position >= 0; --position) {
            const std::uint32_t at = first + static_cast<std::uint32_t>(position) * kHandItemSize;
            if (!state.at(battle::BattlerSlot::character(position)).actor) {
                leaveOut(at, kHandItemSize);
                targeting.reset();
                continue;
            }
            const battle::BattleItem& hand = (*hands)[static_cast<std::size_t>(position)];
            battle::ItemRecord record = battle::recordOf(hand);
            if (hand.item != ItemId::EMPTY) {
                targeting = record[kRecordTargeting];
            } else {
                record[kRecordTargeting] = targeting.value_or(0);
            }
            layAsTheCartridgeHoldsIt(at, record);
            if (!targeting) {
                leaveOut(at + kRecordTargeting, 1);
            }
        }
    }
    for (std::size_t slot = game::kInventorySlots; slot-- > 0;) {
        const battle::BattleItem& item = state.inventory.items[slot];
        const std::uint32_t at = kInventoryRecords + static_cast<std::uint32_t>(slot) * kRecordBytes;
        if (item.item != ItemId::EMPTY) {
            targeting = battle::recordOf(item)[kRecordTargeting];
            continue;
        }
        const std::array<std::uint8_t, kRecordBytes> empty{static_cast<std::uint8_t>(ItemId::EMPTY), kUnusableItem,
                                                          targeting.value_or(0), item.quantity, kNoHandHoldsIt};
        layAsTheCartridgeHoldsIt(at, empty);
        if (!targeting) {
            leaveOut(at + kRecordTargeting, 1);
        }
    }

    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        const battle::Battler& member = state.at(slot);
        const std::uint32_t at = slotIndex(slot);
        if (!member.actor) {
            layWordAsTheCartridgeHoldsIt(kRecordOffset + at, kNoRecord);
        }
        // A seated member's record keeps the boosts its equipment gives in the top two bits of its
        // maximum HP and MP, written there by the start's equipment sums (UpdateEquip :2349-2362,
        // CheckHPBoost :2456-2472).
        if (member.actor && member.record) {
            const game::CharacterRecord& record = state.party.characters[*member.record];
            const battle::DerivedStats derived = battle::deriveEquipment(record);
            const std::uint32_t base = kCharacterRecords + static_cast<std::uint32_t>(*member.record) * kRecordSize;
            layWordAsTheCartridgeHoldsIt(base + kRecordMaxHp,
                                         static_cast<std::uint16_t>((record.maxHp & kMaximumBits) |
                                                                    boostBits(derived.hpBoost) << kBoostShift));
            layWordAsTheCartridgeHoldsIt(base + kRecordMaxMp,
                                         static_cast<std::uint16_t>((record.maxMp & kMaximumBits) |
                                                                    boostBits(derived.mpBoost) << kBoostShift));
        }
        if (!member.controlling) {
            leaveOut(kControlMenus + static_cast<std::uint32_t>(position) * kControlMenuSize, kControlMenuSize);
        }
    }
    leaveOut(kMonsterIndex, kPartySlotsMonsterIndexBytes);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto at = static_cast<std::uint32_t>(i * 2);
        layByteAsTheCartridgeHoldsIt(kToUpdate + at, pendingByte(battler.pendingUpdates));
        layByteAsTheCartridgeHoldsIt(kAnchorCell + at,
                                     static_cast<std::uint8_t>((battler.mayActAfterJump ? kMayActAfterJumpBit : 0) |
                                                               (battler.airAnchored ? 0 : kNotAnchoredBit) |
                                                               (battler.nearFatalSpellAvailable ? kNearFatalSpellAvailableBit : 0)));
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.takesMenuInput ? kTakesMenuInputBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.atbStopped ? kAtbStoppedBit : 0;
        field |= battler.heldByStatus ? kHeldByStatusBit : 0;
        field |= battler.actionQueued ? kActionQueuedBit : 0;
        field |= battler.heldByControl ? kHeldByControlFieldBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        layByteAsTheCartridgeHoldsIt(kOnTheField + at, field);
        // $3AA1,x, every bit a field but 3, which nothing sets (the start's zero fill, InitBattle
        // :6056-6060): 7 its actions go back in line when it can act again (:1345-1349, :1410-1420),
        // 6 its gauge empties after the action, 5 the back row (:7874), 4 a damage-over-time action is
        // waiting, 2 instant death cannot touch it (:6361-6371), 1 Def (:1611, :4091), 0 its next action
        // skips the advance wait (:5738-5741).
        std::uint8_t stance = battler.requeuePending ? kRequeuePendingBit : 0;
        stance |= battler.emptyGaugeAfterAction ? kEmptyGaugeBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.dotActionPending ? kDamageOverTimeWaitingBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.skipAdvanceWait ? kSkipAdvanceWaitBit : 0;
        layByteAsTheCartridgeHoldsIt(kStance + at, stance);
        // $3E4C,x, every bit a field but 5 and 6, which nothing sets (cells/status).
        const battle::BattlerTriggers& triggers = battler.triggers;
        std::uint8_t triggerBits = triggers.retort ? kRetortBit : 0;
        triggerBits |= triggers.monsterRunic ? kMonsterRunicBit : 0;
        triggerBits |= triggers.runic ? kRunicBit : 0;
        triggerBits |= triggers.regenDue ? kRegenDueBit : 0;
        triggerBits |= triggers.poisonDue ? kPoisonDueBit : 0;
        triggerBits |= triggers.piranha ? kPiranhaBit : 0;
        layByteAsTheCartridgeHoldsIt(kTriggers + at, triggerBits);
        if (slot.isMonster() && !battler.monster) {
            layWordAsTheCartridgeHoldsIt(kMonsterIndex + at, kEmptyMonsterSlot);
        }
        if (!battler.monster) {
            // A party member with no rage chosen holds $FF in the rage's low byte, which a Rage aimed at
            // random tests for it (RandRage :965-968) and every Rage sets with its high byte cleared
            // (:964-965, :969-970, Cmd_10 :3342-3345); the high byte nothing reads before that.
            if (!battler.rage) {
                layByteAsTheCartridgeHoldsIt(kNamedMonster + at, kNoRageChosen);
                leaveOut(kNamedMonster + at + 1, 1);
            }
            // Its special attack is read only by an attack it makes as a monster (CalcAttackEffect
            // :8185-8195), after Rage's coming on (SetRage :999-1013, Cmd_10 :3347-3350) or a Sketch
            // (Cmd_0d :3312-3313) has written it; a rage chosen but not yet begun has not.
            if (!battler.status.has(StatusId::RAGE)) {
                leaveOut(kSpecialAttack + at, 1);
            }
        }
        // A battler with no party member in it holds in its main hand the attack graphic of the monster
        // it is or rages as (LoadMonsterProp :7395-7396, LoadRageProp :1010-1012), and in its off hand the
        // zero fill (InitBattle :6056-6060) or the rage's graphic again; an empty slot holds the fill.
        if (!battler.actor) {
            const std::optional<MonsterId> shown = battler.rage ? battler.rage : battler.monster;
            const std::uint8_t graphic =
                shown ? static_cast<std::uint8_t>(getMonsterProperties(*shown).attackGraphic) : std::uint8_t{0};
            const std::array<std::uint8_t, 2> hands{graphic, static_cast<std::uint8_t>(battler.rage ? graphic : 0)};
            layAsTheCartridgeHoldsIt(kHandItems + at, hands);
        }
        // What a battler was last hit with is read only once it has been hit (AICond_01/_02 :4669-4671,
        // AICond_04 :4693-4695 test the "by" cell first, which the fill leaves empty).
        const battle::LastHits& last = battler.lastHit;
        if (!last.command) {
            leaveOut(kLastHitCommand + at, 1);
        }
        if (!last.spell) {
            leaveOut(kLastHitSpell + at, 1);
        }
        if (!last.item) {
            leaveOut(kLastHitItem + at, 1);
        }
        if (!last.element) {
            leaveOut(kLastHitElement + at, 1);
        }
    }
    // The battle's flags at $B1: 0 a counterattack is running (:5721-5722, :12413-12414), 1 the party
    // cannot run and 2 a monster cannot be run from (UpdateMonsterGfxBuf :15179-15249, cells/status), 5 a
    // back attack (:7664-7665), 6 the next action is Mimic's (:325-326, :12975-12976), 7 no battle menu
    // may open (:12074-12075). Bit 4 is one command's own (InitTarget_01 :6529-6530, cleared :3963-3964).
    layByteAsTheCartridgeHoldsIt(
        kRunningCell, static_cast<std::uint8_t>((state.counterattackRunning ? kCounterattackBit : 0) |
                                                (state.escapeBlockedByMonster ? kEscapeBlockedBit : 0) |
                                                (state.cannotRun ? kCannotRunBit : 0) |
                                                (state.backAttack ? kBackAttackFlagBit : 0) |
                                                (state.mimicking ? kMimickingBit : 0) |
                                                (state.menusDisabled ? kMenusClosedBit : 0)));
    // The field's flags at $11E4: bit 0 Gau may come back after the battle, which the start clears when he
    // cannot (InitParty :7775-7785) and anything but a front attack clears (InitBattleType :7588-7589);
    // bit 1 the Veldt; bit 3 the field's music plays on.
    layByteAsTheCartridgeHoldsIt(kFieldFlags, static_cast<std::uint8_t>((state.gauMayAppear ? kGauMayAppearBit : 0) |
                                                                        (state.onTheVeldt ? kOnTheVeldtBit : 0) |
                                                                        (state.keepFieldMusic ? kFieldMusicPlaysOnBit : 0)));
    // Counterattacks are on while $341A is not zero (:12530-12531); every command starts it at $FF
    // (ExecCmd :3106-3109) and a counterattack's end clears it (:9753).
    layByteAsTheCartridgeHoldsIt(kCountersEnabled, state.scratch.countersEnabled ? kFilled : 0);

    // The reach (_c25917 :14635-14642): the monsters the AI leaves in reach in $2F46, the party members
    // not seized in $3403, the targetable or present in $3A78 and those counted in the battle in $3408.
    layByteAsTheCartridgeHoldsIt(kReachMonsters, static_cast<std::uint8_t>(~state.aiUntargetable.monsters.bits));
    layByteAsTheCartridgeHoldsIt(kReachCharacters, static_cast<std::uint8_t>(~seizedSet(state).characters.bits));
    layWordAsTheCartridgeHoldsIt(kReachFirst, maskOf(state.targetableOrPresent));
    layWordAsTheCartridgeHoldsIt(kReachSecond, maskOf(state.countedInBattle));

    const FormationAux& aux = getFormationAux(state.formation);
    layByteAsTheCartridgeHoldsIt(kFormationBytes, static_cast<std::uint8_t>(aux.entranceAndTypes ^ kFormationFirstByteToggled));
    // Which way each battler faces: the graphics program writes it on every frame from its sprites'
    // facings (UpdateFacingDir btlgfx_main.asm:39958-39976), and the battle program only reads it, to find
    // who is hit in the back (:3229-3236, :3253-3260). A whole battle hands each frame the port's facings
    // and compares them; a start runs no frame, so the start's fill stands and they are left out.
    if (facingsHandedOver_) {
        layWordAsTheCartridgeHoldsIt(kFacingRight, maskOf(state.facingRight));
    } else {
        leaveOut(kFacingRight, 2);
    }
    if (state.finalBattleChain) {
        // The final battle's line: each place a stage took a fighter from holds $FF (InitParty
        // :7729-7734), so the fighters still waiting follow the places taken. They are laid from the
        // first place the machine (in a comparison, the cartridge) still holds a fighter in.
        std::uint32_t from = 0;
        while (from < kFinalBattleLineSize && positionByte(kFinalBattleLine + from) == kEndOfLine) {
            ++from;
        }
        if (from + state.finalBattleReserve.size() > kFinalBattleLineSize) {
            from = 0;
        }
        std::array<std::uint8_t, kFinalBattleLineSize> line{};
        line.fill(kEndOfLine);
        for (std::size_t k = 0; k < state.finalBattleReserve.size() && from + k < line.size(); ++k) {
            line[from + k] = static_cast<std::uint8_t>(state.finalBattleReserve[k]);
        }
        layAsTheCartridgeHoldsIt(kFinalBattleLine, line);
    } else if (!state.colosseum) {
        leaveOut(kWagerCell, 1);
        leaveOut(kPrizeCell, 1);
    }

    // The party played and the records' party bytes, which the battle reads and writes only as Gau joins.
    writePartyMembership(state.party);

    layMenusAsTheCartridgeHoldsIt(state);
    layBattlersAsTheCartridgeHoldsIt(state);
    layBattleAsTheCartridgeHoldsIt(state);
    layActionsAsTheCartridgeHoldsIt(state);

    // A party member seated with no record: the start finds none for its actor (InitParty :7848-7910),
    // so its record offset keeps the fill's $FFFF and everything loaded through it comes from bank $7F —
    // its HP, MP, statuses and level (LoadCharProp :6602-6661), what its equipment gives
    // (UpdateEquipBattle :6745-6828), its hands (InitInventory :13919-13929), its commands (InitCmdList
    // :13701-13708) and its esper (ValidateSpellList :14217-14218). No field determines any of it.
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        const battle::Battler& member = state.at(slot);
        if (!member.actor || member.record) {
            continue;
        }
        const std::uint32_t at = slotIndex(slot);
        const auto offset = static_cast<std::uint32_t>(position);
        layWordAsTheCartridgeHoldsIt(kRecordOffset + at, kNoRecord);
        for (const std::uint32_t table : kLoadedThroughTheRecord) {
            leaveOut(table + at, 2);
        }
        leaveOut(kShieldHands + offset * kHandItemSize, kHandItemSize);
        leaveOut(kWeaponHands + offset * kHandItemSize, kHandItemSize);
        leaveOut(kCommandMenus + offset * kCommandMenuSize, kCommandMenuSize);
        leaveOut(kSpellLists + offset * kSpellListSize, kSpellListSize);
        leaveOut(kEsperIndex + at, 2);
        leaveOut(kGenjiGlove + offset, 1);
        leaveOut(kSpellCount + at, 1);
    }
}

void DamageOracle::writeBattleState(const battle::BattleState& state, const PartyCells& boosts) {
    writeAttackCells(DamageScenario{.state = state});
    writeAiCells(AiScenario{.state = state});
    writeAiScriptCells(state);
    PartyCells cells = boosts;
    cells.party = state.party;
    writeEndingScenario(EndingScenario{.cells = cells, .state = state});
    writeStateAsTheCartridgeHoldsIt(state);
}

DamageOracle::LaidState DamageOracle::layState(const battle::BattleState& state, const PartyCells& boosts,
                                               const BattleCells* cartridge) {
    LaidState laid;
    capture_ = &laid;
    positionsFrom_ = cartridge;
    writeBattleState(state, boosts);
    positionsFrom_ = nullptr;
    capture_ = nullptr;
    return laid;
}

std::string DamageOracle::compareBattleState(const battle::BattleState& state, const PartyCells& boosts,
                                             const BattleCells& cartridge) {
    const LaidState laid = layState(state, boosts, &cartridge);
    std::string lines;
    char text[112];
    for (const auto& [cell, bits] : laid.disagreements) {
        if ((bits & comparedBits(laid, cell)) != 0) {
            std::snprintf(text, sizeof text, "$7E:%04X two writers lay it differently (bits %02X)\n", cell & 0xFFFF,
                          bits & comparedBits(laid, cell));
            lines += text;
        }
    }
    for (const auto& [cell, port] : laid.cells) {
        const std::uint32_t offset = cell - kWorkRam;
        if (cell < kWorkRam || offset >= cartridge.size()) {
            continue;
        }
        const std::uint8_t meaning = comparedBits(laid, cell);
        if (((cartridge[offset] ^ port) & meaning) != 0) {
            std::snprintf(text, sizeof text, "$7E:%04X port %02X cartridge %02X (bits %02X)\n", offset, port,
                          cartridge[offset], meaning);
            lines += text;
        }
    }
    return lines;
}

std::string DamageOracle::uncoveredWrites(const battle::BattleState& state, const PartyCells& boosts,
                                          const std::set<std::uint32_t>& written, const BattleCells* cartridge) {
    const LaidState laid = layState(state, boosts, cartridge);
    std::string lines;
    char text[80];
    for (const std::uint32_t cell : written) {
        // A bit the state laid leaves out has no meaning in that state: written, it is still covered.
        const auto bitsLeftOut = laid.bitsLeftOut.find(cell);
        const std::uint8_t compared =
            laid.cells.contains(cell) ? meaningBits(cell) : 0;
        const std::uint8_t stateless = bitsLeftOut == laid.bitsLeftOut.end() ? 0 : bitsLeftOut->second;
        const auto left = static_cast<std::uint8_t>(~(compared | stateless | notComparedBits(cell)));
        if (left != 0 && !laid.leftOut.contains(cell) && !laid.positions.contains(cell)) {
            std::snprintf(text, sizeof text, "$7E:%04X written, bits %02X not compared\n", cell & 0xFFFF, left);
            lines += text;
        }
    }
    return lines;
}

std::string DamageOracle::carriedReads(const battle::BattleState& state, const PartyCells& boosts,
                                       const std::vector<UnwrittenRead>& reads, const BattleCells* cartridge,
                                       CarriedFrom from) {
    const LaidState laid = layState(state, boosts, cartridge);
    std::set<std::uint32_t> named;
    std::string lines;
    char text[80];
    for (const UnwrittenRead& read : reads) {
        // A cell compared in part is held by the bits compared: its row on kCellsNotCompared names
        // every reader of the rest. A return reads what its own call pushed.
        const bool held = (laid.cells.contains(read.at) && comparedBits(laid, read.at) != 0) ||
                          laid.positions.contains(read.at) || laid.carriedToNothing.contains(read.at);
        const bool leftAsItIs =
            from == CarriedFrom::AStep ? leftAsTheMachineHasItInAStep(read.at) : leftAsTheMachineHasIt(read.at);
        if (held || onTheStack(read.at) || leftAsItIs || named.contains(read.at)) {
            continue;
        }
        named.insert(read.at);
        std::snprintf(text, sizeof text, "$7E:%04X read after the comparison, %02X, not compared\n", read.at & 0xFFFF,
                      read.value);
        lines += text;
    }
    return lines;
}

}  // namespace ostinato::trace
