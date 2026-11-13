#include "battle/battle_end.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include "battle/battle_inventory.h"
#include "battle/battle_requests.h"
#include "battle/battle_script.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/leveling.h"
#include "battle/morph.h"
#include "battle/party_setup.h"
#include "battle/rewards.h"
#include "battle/spell_learning.h"
#include "data/battle_magic_points.h"
#include "data/formations.h"
#include "data/monster_items.h"
#include "game/battle_switches.h"
#include "ostinato/attack_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

namespace {

// A monster drops something when it has fallen: dead, petrified or a zombie.
constexpr StatusSet kFallen = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);

// Battles numbered from here on give no magic points.
constexpr std::uint16_t kBattlesWithMagicPoints = 512;

// The gold and gil are kept in three bytes.
constexpr std::uint32_t kLow24Bits = 0xFFFFFF;

// The characters from here on learn no spells: Gogo, Umaro and the guests.
constexpr CharacterPropId kFirstWithoutSpells = CharacterPropId::GOGO;

// The places the win gathers its drops in, one per item, as many as there are monster slots.
struct DropPlace {
    ItemId item = ItemId::EMPTY;
    std::uint8_t count = 0;
};

// What the win has done so far that changes what it does next.
struct WinFlags {
    bool catHoodCounted = false;
    bool magicPointsEarned = false;
    bool curseDispelled = false;
    AbilityMessagesShown abilityMessages;
};

void tell(BattleState& state, BattleMessage message) {
    state.requests.push_back(RunBattleScript{.commands = {message}});
}

void stepBack(BattleState& state) {
    state.requests.push_back(RunBattleScript{.commands = {StepBack{}}});
}

bool learnsSpells(CharacterPropId actor) {
    return static_cast<int>(actor) < static_cast<int>(kFirstWithoutSpells);
}

// Every menu closes; then, unless the fanfare is off, the fanfare (not when the field's music plays
// on) and battle event 1.
void celebrate(BattleState& state) {
    for (int position = 0; position < kPartySlots; ++position) {
        state.requests.push_back(CloseBattleMenu{.character = BattlerSlot::character(position)});
    }
    if (getFormationAux(state.formation).fanfareDisabled() || state.noVictoryFanfare) {
        return;
    }
    if (!state.keepFieldMusic) {
        state.requests.push_back(VictoryFanfare{});
    }
    state.requests.push_back(RunBattleScript{.commands = {BattleEventPlays{.event = kVictoryEvent}}});
}

std::uint8_t magicPointsOf(const BattleState& state) {
    const auto battle = static_cast<std::uint16_t>(state.startingFormation);
    return battle < kBattlesWithMagicPoints ? magicPointsForBattle(battle) : 0;
}

// Each living party member, last slot first: the gold doubled by the first Cat Hood, Terra's Morph
// supply, the experience, and the spells equipment and an esper teach.
void rewardEachMember(BattleState& state, std::uint32_t share, std::uint8_t magicPoints, bool partyHasMagicite,
                      std::uint32_t& gold, WinFlags& flags) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        const Battler& member = state.at(slot);
        if (!state.alive.has(slot) || !member.record || !member.actor) {
            continue;
        }
        // A second Cat Hood does nothing.
        if (member.relicEffects5.has(RelicEffect5::DOUBLE_GP) && !flags.catHoodCounted) {
            flags.catHoodCounted = true;
            gold = (gold << 1) & kLow24Bits;
        }
        if (*member.actor == CharacterPropId::TERRA && partyHasMagicite) {
            flags.magicPointsEarned = true;
            gainMorphSupply(state.party, magicPoints);
        }
        game::CharacterRecord& record = state.party.characters[*member.record];
        addExperience(record, share);
        if (member.relicEffects5.has(RelicEffect5::DOUBLE_EXPERIENCE)) {
            addExperience(record, share);
        }
        if (!learnsSpells(*member.actor)) {
            continue;
        }
        game::SpellProgress& spells = state.party.spells[static_cast<std::size_t>(*member.actor)];
        const EquipmentLearning learning = learnFromEquipment(record, spells, state.switches, magicPoints);
        flags.curseDispelled |= learning.curseDispelled;
        flags.magicPointsEarned |= learning.progressed && partyHasMagicite;
        if (record.esper) {
            flags.magicPointsEarned |= learnFromEsper(spells, *record.esper, magicPoints) && partyHasMagicite;
        }
    }
}

// Each living party member, last slot first: every level it reaches, told of once, with what each
// teaches; then each spell it has just learned, told of.
void levelEachMember(BattleState& state, WinFlags& flags) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        const Battler& member = state.at(slot);
        if (!state.alive.has(slot) || !member.record || !member.actor) {
            continue;
        }
        const auto partySlot = static_cast<std::uint32_t>(position);
        reachEarnedLevels(state, state.party.characters[*member.record], partySlot, true, flags.abilityMessages);
        if (!learnsSpells(*member.actor)) {
            continue;
        }
        auto& progress = state.party.spells[static_cast<std::size_t>(*member.actor)].progress;
        for (std::size_t spell = progress.size(); spell-- > 0;) {
            if (progress[spell] == game::SpellProgress::kSpellJustLearned) {
                progress[spell] = game::SpellProgress::kSpellLearned;
                tell(state, {.message = kMessageLearnedSpell,
                             .firstNumber = static_cast<std::uint32_t>(spell),
                             .secondNumber = partySlot});
            }
        }
    }
}

// The lores Strago saw, last first; Mog's dance; the curse lifted.
void learnTheRest(BattleState& state, const WinFlags& flags) {
    learnLores(state.party, state.loresLearned);
    for (std::size_t lore = state.loresLearned.size(); lore-- > 0;) {
        if (state.loresLearned.test(lore)) {
            tell(state, {.message = kMessageLearnedLore,
                         .firstNumber = static_cast<std::uint32_t>(AttackId::CONDEMNED) +
                                        static_cast<std::uint32_t>(lore)});
        }
    }
    if (learnDance(state.party, state)) {
        tell(state, {.message = kMessageMasteredADance});
    }
    if (flags.curseDispelled) {
        tell(state, {.message = kMessageCurseDispelled});
    }
}

// One random draw per fallen monster, highest slot first: below kRareDropBelow its rare drop,
// otherwise its common one. Each item goes into the first place, highest first, already holding it
// or empty; then each place, highest first, joins the items obtained, is merged and is told of.
void dropItems(BattleState& state) {
    std::array<DropPlace, kMonsterSlots> places{};
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const Battler& monster = state.at(BattlerSlot::monster(position));
        if (!monster.status.hasAny(kFallen)) {
            continue;
        }
        const bool rare = state.rng.next() < kRareDropBelow;
        if (!monster.monster) {
            continue;
        }
        const MonsterItems& items = getMonsterItems(*monster.monster);
        const ItemId drop = rare ? items.rareDrop : items.commonDrop;
        if (drop == ItemId::EMPTY) {
            continue;
        }
        for (std::size_t place = places.size(); place-- > 0;) {
            if (places[place].item == drop || places[place].item == ItemId::EMPTY) {
                places[place].item = drop;
                ++places[place].count;
                break;
            }
        }
    }
    for (std::size_t place = places.size(); place-- > 0;) {
        const DropPlace& dropped = places[place];
        if (dropped.item == ItemId::EMPTY) {
            continue;
        }
        obtainItem(state, dropped.item, dropped.count);
        mergeItemsObtained(state);
        if (dropped.count == 1) {
            tell(state, {.message = kMessageGotItem, .firstNumber = static_cast<std::uint32_t>(dropped.item)});
        } else {
            tell(state, {.message = kMessageGotItems,
                         .firstNumber = static_cast<std::uint32_t>(dropped.item),
                         .secondNumber = static_cast<std::uint32_t>(dropped.count)});
        }
    }
}

void winBattle(BattleState& state, ActionExecutor& executor) {
    stepBack(state);
    celebrate(state);
    wrapUpBattle(state, executor);
    if (state.colosseum) {
        const ItemId prize = state.colosseumPrize.value_or(ItemId::EMPTY);
        obtainItem(state, prize, 1);
        mergeItemsObtained(state);
        tell(state, {.message = kMessageGotItem, .firstNumber = static_cast<std::uint32_t>(prize)});
        return;
    }

    const std::uint8_t magicPoints = magicPointsOf(state);
    const bool partyHasMagicite = state.switches.isSet(game::kMagicPointsEarnedSwitch);
    WinFlags flags;
    const BattleSpoils spoils = spoilsOf(state);
    const std::uint32_t share = experienceShare(spoils.experience, static_cast<std::uint8_t>(state.allyCount));
    if (share != 0) {
        tell(state, {.message = kMessageGotExperience, .firstNumber = share});
    }
    std::uint32_t gold = spoils.gold;
    rewardEachMember(state, share, magicPoints, partyHasMagicite, gold, flags);
    if (partyHasMagicite && flags.magicPointsEarned && magicPoints != 0) {
        tell(state, {.message = kMessageGotMagicPoints, .firstNumber = magicPoints});
    }
    levelEachMember(state, flags);
    learnTheRest(state, flags);
    dropItems(state);
    if (gold == 0) {
        return;
    }
    tell(state, {.message = kMessageGotGp, .secondNumber = gold});
    state.party.gil = std::min((state.party.gil + gold) & kLow24Bits, kMaxGil);
}

// Gau leaves the party once the battle is handed back, and is no longer one of the characters the
// party can field.
void gauLeaves(BattleState& state) {
    for (int position = 0; position < kPartySlots; ++position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.at(slot).actor == CharacterPropId::GAU) {
            state.takenFromTheParty.add(slot);
            state.gauLeftParty = true;
            return;
        }
    }
}

}  // namespace

void learnLevelAbilities(BattleState& state, CharacterPropId actor, std::uint8_t level, AbilityMessagesShown& shown) {
    const AbilitiesLearned learned = learnAbilitiesAtLevel(state.party, actor, level);
    // LEGACY VERBATIM (docs/Bugs.md): one technique message and one blitz message per win.
    if (learned.swdtech && !shown.swdtech) {
        shown.swdtech = true;
        tell(state, {.message = kMessageMasteredATechnique});
    }
    if (learned.blitz && !shown.blitz) {
        shown.blitz = true;
        tell(state, {.message = kMessageDevisedABlitz});
    }
}

bool reachEarnedLevels(BattleState& state, game::CharacterRecord& record, std::uint32_t partySlot, bool tellLevel,
                       AbilityMessagesShown& shown) {
    while (earnedNextLevel(record)) {
        if (tellLevel) {
            tellLevel = false;
            tell(state, {.message = kMessageGainedALevel, .secondNumber = partySlot});
        }
        levelUp(record, state.version);
        learnLevelAbilities(state, record.actor, record.level, shown);
    }
    return tellLevel;
}

void wrapUpBattle(BattleState& state, ActionExecutor& executor) {
    saveMorphSupply(state);
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        state.at(slot).morphGauge = 0;
        state.requests.push_back(CloseBattleMenu{.character = slot});
    }
    state.menusDisabled = true;
    state.requests.push_back(RunBattleScript{.commands = {Wait{.frames = kWrapUpWaitFrames}}});
    state.holdingItem = state.holdingItem | BattlerSet::allCharacters();
    putAwayItemsObtained(state);
    mergeItemsObtained(state);
    applyEquipmentChanges(state, executor);
}

void playBattleEnd(BattleState& state, ActionExecutor& executor, const BattleOutcome& outcome) {
    if (std::holds_alternative<BattleWon>(outcome)) {
        winBattle(state, executor);
        return;
    }
    if (const auto* lost = std::get_if<BattleLost>(&outcome)) {
        wrapUpBattle(state, executor);
        if (lost->message) {
            tell(state, {.message = *lost->message});
        }
        return;
    }
    const auto& ended = std::get<BattleEnded>(outcome);
    switch (ended.reason) {
        case BattleEndReason::GauLeapt:
            // LEGACY VERBATIM (docs/Bugs.md): a Leap ends the battle with no wrap-up.
            gauLeaves(state);
            learnRages(state.party, state);
            return;
        case BattleEndReason::RagesLearned:
            // LEGACY VERBATIM (docs/Bugs.md): nor does a battle that ends by teaching its rages.
            learnRages(state.party, state);
            return;
        case BattleEndReason::FinalBattleNextStage:
            stepBack(state);
            wrapUpBattle(state, executor);
            return;
        case BattleEndReason::RanOutOfTime:
        case BattleEndReason::EndNow:
        case BattleEndReason::TimerExpired:
        case BattleEndReason::EatenByZoneEater:
            wrapUpBattle(state, executor);
            return;
    }
}

}  // namespace ostinato::battle
