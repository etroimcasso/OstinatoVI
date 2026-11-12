// The actions the battle takes on its own: damage over time, a spell no one cast, status timers
// running out, the party getting away, Seize draining its captive, and the things a monster's AI
// asks for besides attacks — a formation change, a dialog, a battle event, monsters leaving or
// coming back, Scan's report, a monster animation, a variable or switch changed, and the effects
// of AiEffect.
//
// CommandExecutor::execute() runs each one when the battle core reaches it. Every function adds
// what the action shows to `script`; the ones that make an attack set it up in `attack` and run it
// (battle/attack.h), so the hooks work out its damage and statuses as for any command:
//
//     AttackInFlight attack;
//     BattleScriptBuilder script;
//     damageOverTime(state, hooks, script, attack, slot, DamageOverTimeKind::Poison);
//     std::vector<BattleScriptCommand> commands = script.take();
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_commands.md.
#pragma once

#include <cstdint>

#include "battle/action_executor.h"
#include "battle/action_queue.h"
#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "ostinato/attack_id.h"

namespace ostinato::battle {

// Poison, Regen or Sap takes effect on `slot`: an attack on itself that ignores defense, is never
// split, and turns around on the undead. Its power comes from the battler's max HP and stamina;
// Regen's restores. Poison's damage multiplier grows with every beat (Battler::poisonMultiplier).
// Nothing happens while no enemy is left, or while the battler is held by a status.
void damageOverTime(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                    BattlerSlot slot, DamageOverTimeKind kind);

// `spell` happens to `target` with no one casting it, as if the target cast it on itself — free,
// unreflected, undodgeable, after the casting swirl. Doom also stops the target's condemned count,
// and does nothing more while no enemy is left.
void spellWithoutCaster(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                        AttackId spell, BattlerSlot target);

// The statuses whose timers ran out come off: Stop, Reflect, Frozen, and Sleep if the battler is
// still asleep, which wakes it with Tapir's animation.
void statusTimersExpire(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                        const StatusTimersExpired& expired);

// The party members who have got away leave the battle: each one whose gauge is full, who can act,
// is on the field and is neither a zombie nor hidden. They run off together. While the party
// cannot run, the "can't run away" message shows instead.
void partyEscapes(BattleState& state, ActionExecutor& executor, BattleScriptBuilder& script);

// `seizer` drains HP from the battler it holds in Seize: level 16, power 16, unreflected, aimed
// exactly, and not capped by the HP `seizer` is missing.
void seizeDrain(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                BattlerSlot seizer, BattlerSlot seized);

// The battle moves to another formation (changeFormation() in battle/battle_start.h) and shows it.
void formationChange(BattleState& state, ActionExecutor& executor, BattleScriptBuilder& script,
                     const FormationChange& change, const TextCorpus& text, const MonsterSprites& sprites);

// Monsters leave the field or come back, each by the action's mode, and the change is shown.
// Only slots that hold a monster are affected.
void setMonsterVisibility(BattleState& state, BattleScriptBuilder& script, const MonsterVisibilityAction& action);

// Scan's report on `target`: its level, its HP, its MP unless it has none, and each element it is
// weak against that nothing else about it cancels — one message each, every message its own
// battle script, played in order before the action's own.
void scan(BattleState& state, BattlerSlot target);

// `actor`'s script sets a variable to the action's amount, or adds it or subtracts it. A battle
// variable is shared by every script in the battle; the battler's own is Battler::aiVariable. The
// result stays within the variable's byte.
void changeBattleVariable(BattleState& state, BattlerSlot actor, const BattleVariableAction& action);

// `actor`'s script toggles, sets or clears a switch: a bit of a variable, or one of the game's
// battle switches (BattleState::switches).
void changeBattleSwitch(BattleState& state, BattlerSlot actor, const BattleSwitchAction& action);

// `actor`'s script's effect (AiEffect):
//
//   ResetOwnTimer / ResetBattleTimer   Battler::turnTimer / BattleState::battleCounter back to 0
//   MakeInvincible / EndInvincible     the target joins or leaves BattleState::invincible
//   MakeTargetable / MakeUntargetable  the target's monsters leave or join BattleState::aiUntargetable
//   EndBattle / EndBattleLearningRages the battle's end-of-battle event: EndNow / RagesLearned
//   RecruitGau                         Gau is recruited, and `actor`'s character record joins the
//                                      party being played at `actor`'s slot, its row kept
//   FillGauge                          if the target names anyone, `actor`'s own gauge fills at the
//                                      next time update — not the target's
//   SetStatus / ClearStatus            an attack by `actor` on itself that puts the status on or
//                                      takes it off, with nothing shown
//   HideSelf                           Hide goes straight onto `actor`'s statuses
//   Nothing                            nothing
//
// A target is found as a script's condition finds one (aiTargets(), with every battler in the
// battle a candidate).
void runAiEffect(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor, const AiEffectAction& action);

}  // namespace ostinato::battle
