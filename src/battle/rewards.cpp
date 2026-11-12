#include "battle/rewards.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "battle/battler_set.h"
#include "battle/spell_learning.h"
#include "data/battle_tables.h"
#include "data/item_properties.h"
#include "data/level_up.h"
#include "data/natural_magic.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

namespace {

// Experience and gil are kept in three bytes.
constexpr std::uint32_t kLow24Bits = 0xFFFFFF;

// A monster gives its experience and gold when it has fallen: dead, petrified or a zombie.
constexpr StatusSet kFallen = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);

// The experience a level needs is this many times the sum of the steps up to it.
constexpr std::uint32_t kLevelExperienceScale = 8;

// The Morph supply holds at most this.
constexpr int kMostMorphSupply = 255;

// A monster numbered at or past this has no rage.
constexpr std::size_t kMonstersWithRages = 256;

// The console's divider: `dividend` over the low byte of `divisor`, with a divisor of 0 answering
// a quotient of 65,535.
struct Quotient {
    std::uint16_t quotient = 0;
    std::uint8_t remainder = 0;
};

constexpr std::uint16_t kQuotientOfDivisionByZero = 0xFFFF;

Quotient divide(std::uint16_t dividend, std::uint8_t divisor) {
    if (divisor == 0) {
        return {.quotient = kQuotientOfDivisionByZero, .remainder = static_cast<std::uint8_t>(dividend & 0xFF)};
    }
    return {.quotient = static_cast<std::uint16_t>(dividend / divisor),
            .remainder = static_cast<std::uint8_t>(dividend % divisor)};
}

// The one ability `table` teaches at `level`, as its bit in the set the party knows: the first entry
// whose level is `level`, or none.
std::uint8_t abilityTaughtAt(std::span<const AbilityLearnLevelEntry> table, std::uint8_t level) {
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        if (table[slot].level == level) {
            return static_cast<std::uint8_t>(1u << slot);
        }
    }
    return 0;
}

// The ability taught at `level` added to `known`. True when it was not known before.
bool learnAbilityAt(AbilityLearnedSet& known, BattleCommandId command, std::uint8_t level) {
    const std::uint8_t bit = abilityTaughtAt(abilityLearnLevels(command), level);
    if (bit == 0 || (known.bits & bit) != 0) {
        return false;
    }
    known.bits = static_cast<std::uint8_t>(known.bits | bit);
    return true;
}

// Each natural spell of `table` whose level is exactly `level` marked just learned, unless learned.
template <std::size_t N>
void learnNaturalMagic(const std::array<NaturalMagicSlot, N>& table, game::SpellProgress& spells,
                       std::uint8_t level) {
    for (const NaturalMagicSlot& row : table) {
        if (row.record.level != level) {
            continue;
        }
        std::uint8_t& learned = spells.progress[static_cast<std::size_t>(row.record.spell)];
        if (learned != game::SpellProgress::kSpellLearned) {
            learned = game::SpellProgress::kSpellJustLearned;
        }
    }
}

}  // namespace

BattleSpoils spoilsOf(const BattleState& state) {
    BattleSpoils spoils;
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const Battler& monster = state.at(BattlerSlot::monster(position));
        if (!monster.status.hasAny(kFallen)) {
            continue;
        }
        if (!state.onTheVeldt) {
            spoils.experience += monster.experience;
        }
        spoils.gold += monster.gold;
    }
    return spoils;
}

std::uint32_t experienceShare(std::uint32_t total, std::uint8_t allies) {
    // A three-byte total over a byte, in two steps of the console's 16-by-8 divide: the top two
    // bytes, then the remainder above the low byte. The second quotient is below 256 whenever the
    // divisor is not 0, so the share is the total's quotient; over 0 both steps answer 65,535 and
    // the share's three bytes are all 255.
    const Quotient high = divide(static_cast<std::uint16_t>(total >> 8 & 0xFFFF), allies);
    const Quotient low = divide(static_cast<std::uint16_t>(high.remainder << 8 | (total & 0xFF)), allies);
    return (static_cast<std::uint32_t>(high.quotient) << 8 | (low.quotient & 0xFF)) & kLow24Bits;
}

void addExperience(game::CharacterRecord& record, std::uint32_t share) {
    const std::uint32_t sum = ((record.experience & kLow24Bits) + (share & kLow24Bits)) & kLow24Bits;
    record.experience = std::min(sum, kMaxExperience);
}

bool earnedNextLevel(const game::CharacterRecord& record) {
    assert(record.level >= 1 && "a character's level is 1 or more");
    if (record.level >= kMaxLevel) {
        return false;
    }
    std::uint32_t steps = 0;
    for (int level = kFirstProgressionLevel; level <= record.level + 1; ++level) {
        steps += levelUpExp(static_cast<std::uint8_t>(level));
    }
    return (record.experience & kLow24Bits) >= steps * kLevelExperienceScale;
}

AbilitiesLearned learnAbilitiesAtLevel(game::Party& party, CharacterPropId actor, std::uint8_t level) {
    AbilitiesLearned learned;
    switch (actor) {
        case CharacterPropId::TERRA:
            learnNaturalMagic(kNaturalMagicTerra, party.spells[static_cast<std::size_t>(actor)], level);
            break;
        case CharacterPropId::CELES:
            learnNaturalMagic(kNaturalMagicCeles, party.spells[static_cast<std::size_t>(actor)], level);
            break;
        case CharacterPropId::CYAN:
            learned.swdtech = learnAbilityAt(party.swdtechsKnown, BattleCommandId::BUSHIDO, level);
            break;
        case CharacterPropId::SABIN:
            learned.blitz = learnAbilityAt(party.blitzesKnown, BattleCommandId::BLITZ, level);
            break;
        default:
            break;
    }
    return learned;
}

EquipmentLearning learnFromEquipment(game::CharacterRecord& record, game::SpellProgress& spells,
                                     game::BattleSwitches& switches, std::uint8_t magicPoints) {
    EquipmentLearning learning;
    ItemId* const slots[] = {&record.weapon, &record.shield, &record.helmet,
                             &record.armor,  &record.relic1, &record.relic2};
    for (ItemId* slot : slots) {
        if (*slot == ItemId::EMPTY) {
            continue;
        }
        if (*slot == ItemId::CURSED_SHLD) {
            const auto battles = static_cast<std::uint8_t>(switches.cursedShieldBattles() + 1);
            switches.setCursedShieldBattles(battles);
            if (battles == 0) {
                learning.curseDispelled = true;
                *slot = ItemId::PALADIN_SHLD;
            }
        }
        const ItemProperties& item = getItemProperties(*slot);
        learning.progressed |= addSpellProgress(spells, item.spellLearned, item.spellLearnRate, magicPoints);
    }
    return learning;
}

void learnLores(game::Party& party, const std::bitset<game::kLores>& learnedThisBattle) {
    party.loresKnown |= learnedThisBattle;
}

std::optional<DanceId> learnDance(game::Party& party, const BattleState& state) {
    bool mogInTheBattle = false;
    for (int position = 0; position < kPartySlots; ++position) {
        mogInTheBattle |= state.at(BattlerSlot::character(position)).actor == CharacterPropId::MOG;
    }
    if (!mogInTheBattle) {
        return std::nullopt;
    }
    const DanceId dance = backgroundDance(state.background);
    const auto bit = static_cast<std::size_t>(dance);
    if (party.dancesKnown.test(bit)) {
        return std::nullopt;
    }
    party.dancesKnown.set(bit);
    return dance;
}

void gainMorphSupply(game::Party& party, std::uint8_t magicPoints) {
    // The doubling's low byte, plus the bit the doubling shifted out, plus the supply.
    const int doubled = magicPoints * 2;
    const int sum = (doubled & 0xFF) + (doubled >> 8) + party.morphSupply;
    party.morphSupply = static_cast<std::uint8_t>(std::min(sum, kMostMorphSupply));
}

void learnRages(game::Party& party, const BattleState& state) {
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const std::optional<MonsterId>& monster = state.at(BattlerSlot::monster(position)).monster;
        if (!monster) {
            continue;
        }
        const auto number = static_cast<std::size_t>(*monster);
        if (number < kMonstersWithRages) {
            party.ragesKnown.set(number);
        }
    }
}

}  // namespace ostinato::battle
