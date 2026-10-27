// Starting a battle from a formation and the party, and handing the party back when it ends.
//
// The field (or the world map, or an event) starts a battle with a BattleStart — which formation,
// against which background, with what the story allows — plus the party, the battle switches and
// the game's text and the monsters' sprites. startBattle() returns the battle ready for its first
// frame: every party member and monster loaded, the battle type chosen, everyone placed on the
// field (battle/battle_layout.h), the gauges filled to where they start, and the opening request
// on BattleState::requests. When the battle has an outcome, writeBack() returns what the game
// keeps from it.
//
//     BattleState state = startBattle(start, party, switches, cartridge.text,
//                                     cartridge.monsterSprites, executor);
//     ... tickBattleTime() / stepBattle() until stepBattle() returns an outcome ...
//     BattleResult result = writeBack(state);
//     party = result.party;
//     switches = result.switches;
//
// The final battle is fought in stages: a won stage ends with BattleEndReason::FinalBattleNextStage,
// and startNextFinalBattleStage() carries the party on into the next one. BattleSim
// (battle/battle_sim.h) runs all of this from a frame tick.
//
// The rules, and the line in the original each comes from, are in docs/contracts/battle_core.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "battle/action_executor.h"
#include "battle/battle_state.h"
#include "data/formations.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/formation_ref.h"
#include "ostinato/game_version.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

namespace ostinato::battle {

// A colosseum match: what the player bet, what winning earns, the monster and the fighter.
struct ColosseumMatch {
    ItemId wager = ItemId::EMPTY;
    ItemId prize = ItemId::EMPTY;
    MonsterId opponent{};
    CharacterPropId fighter = CharacterPropId::TERRA;
};

// Everything a battle starts from besides the party, the switches and the text.
struct BattleStart {
    // The formation, as the encounter tables and events name it: with its randomize flag set,
    // one of it and the three after it is chosen at random. A conditional battle the switches
    // turn on can replace it.
    FormationRef formation;
    BattleBackgroundId background{};

    bool gauMayAppear = false;      // on the Veldt, Gau may come back after this battle
    bool onTheVeldt = false;        // Leap works
    bool keepFieldMusic = false;    // the field's music plays on through the battle

    // The characters the party can field now: whether Gau and Shadow are among them.
    bool gauRecruited = false;
    bool shadowAvailable = false;

    std::optional<ColosseumMatch> colosseum;  // a match in the colosseum (its formations only)

    // The final battle: the character records in the order the player lined them up to step in.
    std::vector<std::size_t> finalBattleOrder;

    BattleConfig config;
    GameVersion version = GameVersion::US_1_1;

    // The frames part of the play clock (0-59) when the battle starts: it decides where the
    // battle's random numbers begin.
    std::uint8_t playTimeFrames = 0;
};

// What the game keeps from a battle.
struct BattleResult {
    BattleOutcome outcome;

    // The party as the battle leaves it: each member's HP, MP and lasting statuses, the inventory
    // with what was used up, and anyone the battle took out of the party.
    game::Party party;

    // The battle switches as the battle leaves them, including how it ended: party defeated, ran
    // out of time, timer expired, swallowed by Zone Eater.
    game::BattleSwitches switches;

    // A formation the party has now met that Gau can find on the Veldt.
    std::optional<FormationId> veldtFormation;

    bool shadowLeftParty = false;  // Shadow walked off when the battle was won
};

// The battle the field asks for, ready for its first frame.
BattleState startBattle(const BattleStart& start, game::Party party, game::BattleSwitches switches,
                        const TextCorpus& text, const MonsterSprites& sprites, ActionExecutor& executor);

// What the game keeps from the battle in `state`, which has an outcome.
BattleResult writeBack(const BattleState& state);

// The steps of a battle's start that startBattle() runs in turn, for code that redoes one of them
// mid-battle.

// The formation fought when `asked` is asked for: a conditional battle whose switch is on replaces
// it (the eight are checked last first, so one replacement can lead to another), and a randomized
// formation becomes itself or one of the three after it.
FormationId chooseFormation(BattleState& state, FormationRef asked);

// Put monster `id` in `slot`: its stats, items, immunities, innate statuses, AI script and name
// group, first strike, and a vigor of 56 to 63.
void loadMonster(BattleState& state, ActionExecutor& executor, BattlerSlot slot, MonsterId id,
                 const TextCorpus& text);

// What a monster brings to whoever takes it on — a monster loaded into its slot, or a party member
// raging as it: its special attack and metamorph, its elements and immunities added to the
// battler's, the statuses its record keeps on it, and its True Knight cover. Returns the statuses
// the monster starts with (Float for a flier, Reraise and Float of its fourth status byte); the
// caller decides when they are put on.
StatusSet loadMonsterTraits(Battler& battler, MonsterId id);

// The battle type, from the formation's possible types (FormationAux::possibleBattleTypes()):
// a Back Guard rules out pincer and back attacks and a party of fewer than three rules out side
// attacks, each unless nothing else is possible.
BattleType chooseBattleType(BattleState& state, std::uint8_t possibleTypes);

// What the battle type does to the party: the chance of a preemptive strike in a front or side
// attack, the rows in a back or pincer attack. Anything but a front attack keeps Gau away.
void applyBattleType(BattleState& state, const FormationAux& aux);

// Where every gauge starts.
void fillStartingGauges(BattleState& state);

// The final battle's next stage, after `finished` won its stage and `handedBack` is what it
// handed back. The fighters still standing, and the statuses they carry, go on; a fallen
// fighter's place goes to the next character in line.
BattleState startNextFinalBattleStage(const BattleState& finished, const BattleResult& handedBack,
                                      const BattleStart& start, const TextCorpus& text,
                                      const MonsterSprites& sprites, ActionExecutor& executor);

}  // namespace ostinato::battle
