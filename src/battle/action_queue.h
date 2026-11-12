// What a battler can have waiting to happen, and the queues that decide whose turn is next.
//
// An Action is one thing a battler will do: a battle command with its attack and targets, or one
// of the things the battle does on its own — damage over time, a status running out, a spell no
// one cast, the party fleeing. Each battler keeps its own list of actions in the order they were
// chosen, and a second list for counterattacks.
//
// A BattlerQueue is a line of battlers waiting for something. The battle keeps three: the battlers
// waiting out their advance wait (the pause between choosing an action and taking it), the
// battlers whose action is ready to run, and the battlers with a counterattack to make. A battler
// can be in a line more than once; erase() takes it out everywhere.
//
//     BattlerQueue ready;
//     ready.push(BattlerSlot::monster(0));
//     ready.push(BattlerSlot::character(1));
//     ready.pop();                                      // monster 0
//
// The rules for when battlers join and leave each line are specified in
// docs/contracts/battle_core.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>

#include "battle/battle_script.h"
#include "battle/battler_set.h"
#include "data/ai_scripts.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_visibility_mode.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

// A battle command: FIGHT, MAGIC, STEAL and the rest, with the attack it uses (a spell, a blitz,
// an item, depending on the command), who it is aimed at, and the MP it costs.
struct CommandAction {
    BattleCommandId command = BattleCommandId::FIGHT;
    AttackId attack{};
    BattlerSet targets;
    std::uint8_t mpCost = 0;
};

// The battler's AI decides what to do when its turn comes. A monster whose turn arrives with
// nothing chosen gets one of these, and so does a battler whose counterattack script answers an
// attack. `answering` is the battler a counterattack answers, empty when it answers its own death
// with no one to blame; the counterattack's script starts out aimed at it. A turn's is always empty.
struct AiTurn {
    BattlerSet answering;
};

// What a damage-over-time action takes or gives.
enum class DamageOverTimeKind : std::uint8_t {
    Poison,  // Poison status
    Regen,   // Regen status
    Sap,     // Sap status, or Phantasm
};

// The battler's poison, regen or sap takes effect.
struct DamageOverTime {
    DamageOverTimeKind kind = DamageOverTimeKind::Poison;
};

// A spell that happens to a battler with no one casting it: Doom when a condemned count runs out,
// the spells a near-fatal relic casts, Life when Reraise takes effect.
struct SpellWithoutCaster {
    AttackId spell{};
    BattlerSlot target = BattlerSlot::character(0);
};

// One or more of a battler's status timers ran out this update.
struct StatusTimersExpired {
    BattlerSlot battler = BattlerSlot::character(0);
    bool stop = false;
    bool reflect = false;
    bool freeze = false;
    bool sleep = false;
};

// The characters who have got away leave the battle.
struct PartyEscape {};

// A character goes back to standing normally.
struct ResetBattlePose {
    BattlerSlot character = BattlerSlot::character(0);
};

// A battler holding another in Seize drains it.
struct SeizeDrain {
    BattlerSlot seized = BattlerSlot::character(0);
};

// A scripted battle event plays.
struct BattleEventAction {
    std::uint8_t event = 0;
};

// A battle message is shown, such as the one naming a back attack as the battle opens.
struct BattleMessageAction {
    std::uint8_t message = 0;
};

// A battler reached the front of the line with nothing to do: its turn passes with no effect.
struct NoAction {};

// The battle moves to another formation: the monsters on the field leave by `exit` and
// `formation`'s come in. With `restoreMonsters` each new monster starts at its full HP and MP;
// without it, a new monster takes the HP and MP of the one its slot held before.
struct FormationChange {
    FormationId formation{};
    bool restoreMonsters = false;
    bool scrollBackground = false;  // the background scrolls on first, as between the final battle's stages
    MonsterEntranceType exit = MonsterEntranceType::PRE_DRAWN;
};

// A monster says something.
struct MonsterDialogAction {
    std::uint16_t dialog = 0;
};

// What showing or hiding monsters does to each of them (ostinato/monster_visibility_mode.h).
using ostinato::MonsterVisibilityMode;

// Monsters leave the field or come back, shown by `type`.
struct MonsterVisibilityAction {
    MonsterVisibilityMode mode = MonsterVisibilityMode::ReviveAtFullHp;
    BattlerSet monsters;
    MonsterEntranceType type = MonsterEntranceType::PRE_DRAWN;
};

// Scan shows what it learned about its target.
struct ScanAction {
    BattlerSlot target = BattlerSlot::monster(0);
};

// A monster animation plays outside any attack (MonsterAnimationPlays).
struct MonsterAnimationAction {
    MonsterAnimation animation = MonsterAnimation::FlashRed;
    BattlerSet monsters;
    std::uint8_t soundEffect = 0;
    std::uint8_t pan = 0;
};

// A monster's script sets one of its variables, or adds to it or subtracts from it
// (data/ai_scripts.h). The sum stays within the variable's byte: 255 + 1 is 0, 0 - 1 is 255.
struct BattleVariableAction {
    AiVariable variable;
    AiVariableChange change = AiVariableChange::Set;
    std::uint8_t amount = 0;  // at most 63
};

// A monster's script toggles, sets or clears a switch: a bit of one of its variables, or one of
// the game's battle switches.
struct BattleSwitchAction {
    AiSwitch which;
    AiSwitchChange change = AiSwitchChange::Set;
};

// A monster's script asks for one of the effects that are not attacks (AiEffect). `target` is read
// by the effects that name one, `status` by SetStatus and ClearStatus.
struct AiEffectAction {
    AiEffect effect = AiEffect::Nothing;
    AiTarget target;
    StatusId status{};
};

using Action = std::variant<CommandAction, AiTurn, DamageOverTime, SpellWithoutCaster, StatusTimersExpired,
                            PartyEscape, ResetBattlePose, SeizeDrain, BattleEventAction, BattleMessageAction,
                            NoAction, FormationChange, MonsterDialogAction, MonsterVisibilityAction, ScanAction,
                            MonsterAnimationAction, BattleVariableAction, BattleSwitchAction, AiEffectAction>;

// The battle events the battle's end starts: Shadow walking off, and Gau turning up on the Veldt.
inline constexpr std::uint8_t kShadowLeavesEvent = 11;
inline constexpr std::uint8_t kGauJoinsEvent = 27;

// A line of battlers waiting their turn for something.
class BattlerQueue {
public:
    void push(BattlerSlot slot) { slots_.push_back(slot); }

    // Take the battler at the front of the line out of it.
    std::optional<BattlerSlot> pop();

    // Take `slot` out of the line wherever it stands.
    void erase(BattlerSlot slot);

    bool contains(BattlerSlot slot) const;

    bool empty() const { return slots_.empty(); }
    std::size_t size() const { return slots_.size(); }

    // The battler at position `i` from the front.
    BattlerSlot at(std::size_t i) const { return slots_.at(i); }

private:
    std::deque<BattlerSlot> slots_;
};

}  // namespace ostinato::battle
