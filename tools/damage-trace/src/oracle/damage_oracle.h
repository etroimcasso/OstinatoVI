// The damage oracle: the cartridge's own damage routines, isolated and run on the hosted cartridge
// over a battle the port can also work out, so the port's numbers are checked against the game's.
//
// The cartridge is hosted on the engine's SNES machine and started from a tick of the engine's run
// loop. An escape on the game's reset code, at the first instruction after its switch out of
// emulation mode, ends the start: the processor is in the mode the game's code is written for and
// nothing of the game has run yet. Each damage routine is bound where the cartridge holds it and
// called in that context. A caller builds one DamageScenario from the port's own types; the oracle
// writes it into the machine's work RAM at the cells the routine reads, calls the routine, and
// gives back what it wrote — read back from the cells, with watches on them recording each write as
// the routine makes it.
//
//     trace::runOnLoop([&] {
//         trace::DamageOracle oracle{image};
//         oracle.start();
//         trace::DamageScenario scenario;
//         scenario.attack.record.power = 150;
//         ...
//         const std::uint16_t theirs = oracle.calcDamage(scenario);
//         const std::uint16_t ours = battle::physicalDamage(scenario.state, scenario.attack, scenario.attacker);
//     });
//
// runOnLoop() is a run loop on the console's cadence with no window, stepped as fast as the host
// allows, whose one tick runs the work and whose run ends through the loop's exit; a tool with a
// loop of its own starts and calls the oracle from its own tick instead.
//
// Every cell the oracle writes is filled from a field of the port's state, so the guest never sees
// a value the port has no name for; cells a routine does not read are left as the machine has them.
// The routines are at the addresses both US cartridges hold them; the oracle checks each one's
// opening bytes and refuses an image that does not have them. This header gives the scenarios,
// results and port answers of every concern (scenarios/) and the trace (trace/) with the class. The
// members are defined across oracle/ by concern; `ostinato-damage-trace --check` runs the oracle
// from the command line, and tools/damage-trace/README.md lists the tests that run it.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "retropp/clock.h"
#include "retropp/run_loop.h"
#include "retropp/vm.h"

#include "battle/ai.h"
#include "battle/ai_conditions.h"
#include "battle/ai_targets.h"
#include "battle/attack.h"
#include "battle/attacker_effects.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/damage.h"
#include "battle/items_obtained.h"
#include "battle/leveling.h"
#include "battle/rewards.h"
#include "battle/spell_learning.h"
#include "battle/status.h"
#include "battle/target_effects.h"
#include "data/ai_scripts.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"

#include "cells/damage/per_target.h"
#include "scenarios/ai/ai_commands.h"
#include "scenarios/ai/ai_run.h"
#include "scenarios/ai/ai_targets.h"
#include "scenarios/ai/own_actions.h"
#include "scenarios/battle/battle_cells.h"
#include "scenarios/battle/start.h"
#include "scenarios/battle/whole_battle.h"
#include "scenarios/damage/attack.h"
#include "scenarios/damage/cover.h"
#include "scenarios/damage/damage.h"
#include "scenarios/ending/ending.h"
#include "scenarios/ending/records.h"
#include "scenarios/ending/rewards.h"
#include "scenarios/status/attacker_effects.h"
#include "scenarios/status/status.h"
#include "scenarios/status/target_effects.h"
#include "trace/trace_log.h"
#include "trace/trace_parse.h"

namespace ostinato::trace {

// A clock that moves one tick per reading, so a loop run with it steps as fast as the host allows
// instead of waiting for the wall clock.
class TickClock final : public retropp::Clock {
public:
    explicit TickClock(std::chrono::nanoseconds period) noexcept : period_(period) {}
    [[nodiscard]] std::chrono::nanoseconds now() const noexcept override {
        now_ += period_;
        return now_;
    }

private:
    std::chrono::nanoseconds period_;
    mutable std::chrono::nanoseconds now_{};
};

class DamageOracle {
public:
    // Host `image` (a cartridge image without a copier header), declare the escape and the watches,
    // and bind the routines. Throws std::runtime_error when a routine is not where the US cartridges
    // hold it, and whatever the engine throws for an image it cannot host.
    explicit DamageOracle(std::span<const std::uint8_t> image);
    ~DamageOracle();

    // Start the cartridge as far as its reset code's switch to native mode and park it there. Called
    // from a tick of the run loop, once, before any routine is called. Throws std::runtime_error when
    // the reset code is not reached.
    void start();

    DamageOracle(const DamageOracle&) = delete;
    DamageOracle& operator=(const DamageOracle&) = delete;

    // The spell formula over a power, a magic stat and a level (CalcMagicDmg).
    [[nodiscard]] std::uint16_t magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level);

    // The base damage over the scenario, before the relic bonus (CalcDmg).
    [[nodiscard]] std::uint16_t calcDamage(const DamageScenario& scenario);

    // The relic bonus over the scenario, starting from `damage` (RelicDmgEffect).
    [[nodiscard]] std::uint16_t relicBonus(const DamageScenario& scenario, std::uint16_t damage);

    // The multiplier over the scenario's damage and multiplier steps (ApplyDmgMult).
    [[nodiscard]] MultiplierResult applyMultiplier(const DamageScenario& scenario);

    // The damage to `target` over the scenario (CalcTargetDmg): every battler's cells the pass reads
    // written from the scenario, then read back.
    [[nodiscard]] TargetDamageResult targetDamage(const DamageScenario& scenario, battle::BattlerSlot target);

    // The damage in the scenario's ledger taken and shown, with the scenario's attacker as the one
    // who made the attack (the cartridge's apply-and-show routine, which takes each battler's
    // entries off its HP or MP and writes the numeral commands into the battle script).
    [[nodiscard]] ApplyDamageResult applyDamage(const DamageScenario& scenario);

    // The spell in the scenario bounced off the battlers in its attack.params.reflectedOff, or a
    // Launcher's or Super Ball's hits made (CalcReflectDmg). The routine runs the cartridge's own
    // status and special-effect passes for each bounce that hits, as the port's hooks do; this
    // comparison is written for spells neither pass changes anything for: no statuses or special
    // effect, no element that melts Frozen on a Frozen target, no Vanished target, no miss for a
    // status the target is immune to.
    [[nodiscard]] ReflectedAttackResult reflectedAttack(const DamageScenario& scenario);

    // Every present battler's waiting statuses taking effect over the scenario (UpdateStatus): every
    // battler's cells the pass reads written from the scenario, then read back.
    [[nodiscard]] StatusChangesResult statusChanges(const DamageScenario& scenario);

    // The statuses of the scenario's attack on `target`, one of the targets it hits
    // (MagicStatusEffect), with the scenario's attacker as the one who made it.
    [[nodiscard]] StatusEffectResult statusEffect(const DamageScenario& scenario, battle::BattlerSlot target);

    // The special effect of the scenario's attack on the scenario's attacker (DoAttackerEffect), called
    // as the attack frame calls it, before the hit check.
    [[nodiscard]] AttackerEffectResult attackerEffect(const DamageScenario& scenario);

    // The special effect of the scenario's attack on `target`, one of the targets it hits
    // (DoTargetEffect), with the scenario's attacker as the one who made it — called as the attack
    // frame calls it, after the attack's statuses on the target and with the frame's animation
    // already in the script. The scenario holds what the frame leaves there: the target out of the
    // targets hit, already marked to answer the attacker, and its last command hit recorded.
    [[nodiscard]] TargetEffectResult targetEffect(const DamageScenario& scenario, battle::BattlerSlot target);

    // The attack's animation set up over the scenario (InitGfxParams), called as the attack frame
    // calls it: after the aim and the cover, with the attacks still to come and the Dragon Horn as
    // the scenario holds them.
    [[nodiscard]] ParamsInitResult initParams(const DamageScenario& scenario);

    // The cover over the scenario (CoverEffect), called as the attack frame calls it: after the aim,
    // with the accumulator wide and the index registers narrow. The scenario's targets are the aim's.
    [[nodiscard]] CoverResult coverEffect(const DamageScenario& scenario);

    // The spells the scenario record's esper teaches learned further (LearnGenjuMagic), called as a
    // won battle's end calls it for a party member with an esper: the record written at its place
    // among the sixteen, the character's spells known at theirs, the battle's magic points, and the
    // party holding magicite so that a spell stored shows in the win's flags. The record must have an
    // esper.
    [[nodiscard]] SpellLearningResult learnFromEsper(const RecordScenario& scenario);

    // One level-up of the scenario's record (DoLevelUp), called as a won battle's end calls it: the
    // record written at its place with the boosts in the top of its maximum HP and MP words. The
    // record's level must be below 99.
    [[nodiscard]] LevelUpResult levelUp(const RecordScenario& scenario);

    // The steps of a win's rewards, each called as the win calls it, the party written into its cells
    // (writeParty()), the switches, the win's flags, the magic points, the scenario record's actor's
    // spells as the ones being learned and the message variables as the win leaves them:
    //
    // The share added to the scenario record's experience (AddExp): the record in X, the accumulator
    // narrow and the index registers wide.
    [[nodiscard]] RewardResult addExperience(const RewardScenario& scenario);

    // The spells the scenario record's equipment teaches (LearnItemMagic), the same way. The record's
    // actor must be one of the first twelve characters.
    [[nodiscard]] RewardResult learnFromEquipment(const RewardScenario& scenario);

    // The levels the scenario record reaches (CheckLevelUp), the same way, with "gained a level" still
    // to be told or not and the scenario's party slot in the second message variable.
    [[nodiscard]] RewardResult levelUps(const RewardScenario& scenario);

    // The abilities of the scenario's actor at the scenario's level (LearnAbilities): the actor in the
    // accumulator's low byte and the level in its high byte.
    [[nodiscard]] RewardResult learnAbilities(const RewardScenario& scenario);

    // The rages of the six monster slots (LearnRage), both register widths narrow.
    [[nodiscard]] RewardResult learnRages(const RewardScenario& scenario);

    // Every call the last of those made to the graphics program, in order. While one of them runs, a
    // call to the graphics program is answered here: the commands that wait on the screen or the sound
    // return at once, and the two that keep the items obtained ($05, which adds one to the list, and
    // $0A, which merges the list into the inventory) run as the cartridge holds them, each found
    // through the cartridge's own table of the commands.
    [[nodiscard]] const std::vector<GraphicsCall>& graphicsCalls() const { return graphicsCalls_; }

    // One call to the graphics program as the battle program makes it (ExecBtlGfx), the command in the
    // accumulator, from the cells as they stand, answered as above. Returns the calls it made.
    const std::vector<GraphicsCall>& graphicsCommand(std::uint8_t command);

    // The battle's end as every ending but two begins it (_c24903), from the scenario's cells, both
    // register widths narrow, with the graphics program answered as above.
    [[nodiscard]] EndingResult wrapUp(const EndingScenario& scenario);

    // The win, whole (WinBattle), the same way, the accumulator narrow.
    [[nodiscard]] EndingResult win(const EndingScenario& scenario);

    // The loss (LoseBattle), the message in the accumulator — $FF for none.
    [[nodiscard]] EndingResult loss(const EndingScenario& scenario, std::optional<std::uint8_t> message);

    // What the game keeps from the battle (UpdateSRAM), from the scenario's cells, both register widths
    // narrow.
    [[nodiscard]] WriteBackResult writeBack(const EndingScenario& scenario);

    // The scenario's item added to the list (graphics command $05), and the list merged into the
    // inventory ($0A), each through the battle program's call to the graphics program.
    [[nodiscard]] ItemsObtainedResult addItemObtained(const ItemsObtainedScenario& scenario);
    [[nodiscard]] ItemsObtainedResult mergeItemsObtained(const ItemsObtainedScenario& scenario);

    // The cartridge's title program laid over work RAM as a console just switched on lays it
    // (DecompCutsceneProg), and the last three places of the items obtained read back.
    [[nodiscard]] battle::ItemsObtainedTail itemsObtainedTailAtPowerOn();

    // The battlers `target` names for the scenario's actor (CheckAITarget), with or without the cut
    // to the battlers an attack can reach, called as a script's target step and its conditions call
    // it: the operand in the accumulator, the actor in Y, the cut asked for by a clear carry.
    [[nodiscard]] AiTargetsResult aiTargets(const AiScenario& scenario, AiTarget target, bool reachableOnly);

    // The scenario's actor's script testing `condition` (the condition's own routine, found through
    // the cartridge's table of them), called as the script's reader calls it: the condition's bytes
    // where the reader leaves them, the actor in Y and the carry clear. The condition must be one a
    // script can hold.
    [[nodiscard]] AiConditionResult aiCondition(const AiScenario& scenario, const AiCondition& condition);

    // `slot`'s script readied (InitAI), called as loading a monster calls it: the script's index in
    // the accumulator and the slot in Y, both wide. The slot's script cells start as the scenario's
    // state holds them, so what the routine leaves alone shows.
    [[nodiscard]] AiScriptPlaces readyAi(const AiScenario& scenario, battle::BattlerSlot slot);

    // The scenario's actor's turn (ExecMonsterAction), called as taking a turn calls it: the actor in
    // X, no targets and no counterattack running.
    [[nodiscard]] AiRunResult aiTurn(const AiScenario& scenario);

    // The scenario's actor's counterattack (ExecAIRetal), called as making a counterattack calls it:
    // the actor in X, the scenario's `answering` as the targets and a counterattack running.
    [[nodiscard]] AiRunResult aiCounter(const AiScenario& scenario);

    // A turn after the last aiTurn()/aiCounter(), on the cartridge's own state from then: only the
    // actor's lists are emptied first, so what the runs carry from one to the next (the places,
    // the counts, the marks, the random numbers) is the cartridge's alone.
    [[nodiscard]] AiRunResult nextAiTurn(battle::BattlerSlot actor);

    // The command `action` (a BattleVariableAction, BattleSwitchAction or AiEffectAction) for the
    // scenario's actor (Cmd_2e, Cmd_2f or Cmd_30), called as running a command calls it: the cells a
    // command starts with, the actor in Y and the action's three bytes where the command reads them.
    // Every cell the attack frame reads is written as attack() writes it, from a scenario whose
    // attack is the one a command starts with, so SetStatus's and ClearStatus's attack runs whole.
    [[nodiscard]] AiCommandResult aiCommand(const AiScenario& scenario, const battle::Action& action);

    // One of the battle's own actions for the scenario's attacker — damage over time (Cmd_22), a spell
    // with no caster (Cmd_26), status timers running out (Cmd_29), Seize's drain (Cmd_2d), or a
    // script's effect (Cmd_30, run as aiCommand() runs it) — called as running a command calls it:
    // every cell the attack frame reads written as attack() writes it, from the attack a command
    // starts with; the cells a command starts with; the enemies left, every battler's damage-over-time
    // action waiting, its hold by a status and its Poison multiplier; the actor in Y, and the action's
    // command, attack byte and targets where the command reads them. Read back as aiCommand() reads an
    // attack's cells, with every battler's damage-over-time action waiting and Poison multiplier.
    [[nodiscard]] AttackResult ownAction(const DamageScenario& scenario, const battle::Action& action);

    // Every attack of the scenario's attack (ExecAttack), made by the scenario's attacker and called
    // as a command's body calls it: every cell the frame reads written from the scenario, the
    // script's queue and params buffer empty, the attacker in X.
    [[nodiscard]] AttackResult attack(const DamageScenario& scenario);

    // The same aimed at the attacker itself (ExecSelfAttack).
    [[nodiscard]] AttackResult selfAttack(const DamageScenario& scenario);

    // The battle program whole (Battle_ext, $C2:0000), called as the field calls it, from the
    // scenario's cells. Its calls to the graphics program are answered as above, each with no frame
    // and the battle's time stopped. At the loop's first frame wait (BattleLoop's graphics command $01,
    // battle/battle_main.asm:72-73) the battle's cells are read, and the battle is then ended by the
    // cartridge's own end-of-battle event 4, "end the battle now" (:11892-11897, :11983-11985): the
    // wrap-up, what the game keeps and the close run, and the call returns.
    [[nodiscard]] StartResult battleStart(const StartScenario& scenario);

    // The same from the cells as they stand: for a machine restored to the tick before a battle.
    [[nodiscard]] StartResult battleStartAsItStands();

    // The monsters' sprites a whole battle's opening lays the monsters out by, for the sides of a
    // pincer. Required before battleStart(); kept by reference.
    void useMonsterSprites(const MonsterSprites& sprites) { monsterSprites_ = &sprites; }

    // The game's text the port's start reads in checkStart(). Kept by reference.
    void useText(const TextCorpus& text) { text_ = &text; }

    // What useMonsterSprites() and useText() handed the oracle; each throws std::logic_error when it
    // was handed none.
    [[nodiscard]] const MonsterSprites& monsterSprites() const;
    [[nodiscard]] const TextCorpus& text() const;

    // The cartridge's start over `scenario` (battleStart()) against the port's (portStart()): the
    // cells compared (compareBattleState()), then the three gates — every cell the start wrote is
    // covered (uncoveredWrites()), the battle's end read nothing after the loop's first frame wait that
    // the comparison does not hold (carriedReads()), and the start read nothing it was not handed,
    // in the battle's cells or past them — one line per difference, each gate's lines named. Needs
    // useMonsterSprites() and useText().
    [[nodiscard]] StartCheck checkStart(const StartScenario& scenario);

    // The battle program's whole battle over `scenario`, from its start to its end, a loop step at a
    // time: InitRAM and InitBattle called as BattleMain calls them, then BattleLoop called once per step,
    // each call ended when the step comes back round to the loop's head, the last ended at
    // TerminateBattle once its graphics command $09 is made. The graphics program's commands are answered
    // as battleStart() answers them, and the frames the scenario's model gives — the opening's for $00,
    // the script's for $04, one for the loop's frame wait — are run: the frame counter ($0E) and the play
    // clock's frames ($021E) stepped as the interrupt steps them, the story countdown's flag ($1DD1 bit 5)
    // set on the frame it runs out, and the battle's clock (UpdateBattleTime) called on every frame the
    // model says it is. The cells are read at each loop step's frame wait, before its frame. A battle not
    // ended by the scenario's frame cap is ended by the cartridge's own end-of-battle event 4 and marked
    // timed out. Needs useMonsterSprites().
    [[nodiscard]] WholeBattleResult wholeBattle(const BattleScenario& scenario);

    // The same from the cells as they stand, for a machine restored to the tick before a battle.
    [[nodiscard]] WholeBattleResult wholeBattleAsItStands(const PresentationModel& model, std::uint64_t frameCap);

    // The same battle as one call of the battle program's entry (Battle_ext), as the field makes it, with
    // the same answers and frames and the cells read at the same frame waits: for proving the steps add up
    // to the whole. A battle longer than the engine's guard on one bound call cannot be run so; the
    // result then holds what was made before the call was abandoned.
    [[nodiscard]] WholeBattleResult wholeBattleInOneCall(const BattleScenario& scenario);

    // The cartridge's whole battle over `scenario` (wholeBattle()) against the port's (portBattle()):
    // the steps counted alike, each step's state compared (compareBattleState()) with the write-set and
    // carried-read gates, the requests compared in order, and the battle's end — one line per difference,
    // stopping at the first step that parts. Needs useMonsterSprites() and useText().
    [[nodiscard]] BattleCheck checkBattle(const BattleScenario& scenario);

    // The scenario's cells as battleStart() writes them, with nothing called: the field's cells the
    // start reads.
    void writeStartScenario(const StartScenario& scenario);

    // The start as the cells hold it, read the way writeStartScenario() writes one: for a machine
    // restored to the tick before a battle, or one a scenario was written into.
    [[nodiscard]] StartScenario readStartScenario();

    // `state` laid into the battle's cells: the cells a BattleState field determines, through the
    // writers every routine's scenario is written by — the attack frame's, the AI's, the scripts' and
    // the ending's — with none of the attack, command or script those writers also lay, and the
    // party's records holding `boosts`' HP and MP boost bits — and the cells no routine's scenario
    // writes: the party's menus, spell lists and skills, each battler's clocks and counters, and the
    // battle's flags, lines, records and Mimic's memory. Where a field says a place holds nothing, the
    // cell holds what the cartridge's start leaves there.
    void writeBattleState(const battle::BattleState& state, const PartyCells& boosts);

    // The cells writeBattleState() lays `state` into, each by the bits of it that have meaning
    // (kCellMeanings), against the cells a cartridge's battle holds: one line per cell where they part,
    // naming the cell and both bytes, and one per cell two writers lay differently at a bit with
    // meaning. Empty when they agree.
    [[nodiscard]] std::string compareBattleState(const battle::BattleState& state, const PartyCells& boosts,
                                                 const BattleCells& cartridge);

    // The write-set gate: every cell in `written` that compareBattleState() does not cover in full —
    // its bits with meaning and its bits on kCellsNotCompared between them short of the whole byte,
    // and the cell neither left out as no field determines it nor a ring's start — one line per cell
    // naming it and the bits left. Empty when every cell written is covered. The lines are laid from
    // the ring positions in `cartridge` where given (a whole battle's step), else from the machine as it
    // stands (the start, parked at its point).
    [[nodiscard]] std::string uncoveredWrites(const battle::BattleState& state, const PartyCells& boosts,
                                              const std::set<std::uint32_t>& written,
                                              const BattleCells* cartridge = nullptr);

    // The carried-read gate: every read in `reads` (StartResult::readsAfterThePoint) of a cell
    // compareBattleState() does not hold for `state` — on kCellsNotCompared, or left out as no field
    // determines it — one line per cell naming it and the byte the read found. A cell compared in
    // part is held by its compared bits (its row on kCellsNotCompared names every reader of the rest),
    // a ring's start is held by what is laid from it, and the stack is never carried. Empty when the
    // battle read nothing the comparison did not hold. The lines are laid from the ring positions in
    // `cartridge` where given (the cells at the step the reads carry from), else from the machine as it
    // stands.
    [[nodiscard]] std::string carriedReads(const battle::BattleState& state, const PartyCells& boosts,
                                           const std::vector<UnwrittenRead>& reads,
                                           const BattleCells* cartridge = nullptr);

    // Every cell of the battle's work RAM ($7E:0000-3FFF) the last call read before anything wrote
    // it — the oracle's own writes since the call before, or the guest's during the call — with the
    // byte its first such read found, in address order. Empty when every cell the routine read came
    // from the scenario or from the routine itself.
    [[nodiscard]] std::vector<UnwrittenRead> unwrittenReads() const;

    // The writes the last call made to the watched cells, in order: the damage word at
    // $7E:11B0-11B1 and the multiplier's steps at $7E:00BC.
    [[nodiscard]] const std::vector<WatchedWrite>& writes() const { return writes_; }

    // Replay one call the trace logged against the port: the battle is rebuilt from the cells the
    // routine read (every other cell as the oracle writes a default scenario), the attacker and target
    // from the attack's context and the cells the routine touched, and the port's answer is compared
    // with the cells the routine wrote. The statuses taking effect replay from any of their callers;
    // an attack's statuses on one target take the target from the battler cells they touched. An
    // attack's special effect takes its attacker from the params block (for a bounce, the battler it
    // bounced off, or for a Launcher's or Super Ball's hits the lowest battler in the bounces that hit)
    // and its effect on one target takes the call's target (TracedCall::target). Returns every cell
    // where they part, one per line; empty when they agree. Throws std::runtime_error for a call it
    // cannot rebuild, naming why.
    //
    // A battler's AI rebuilds as its scenario: the cells as the oracle writes a default one, then
    // every cell the routine read, read back as an AiScenario. A script readied is the slot whose
    // turn place the call wrote, and the script whose turn part starts there. A turn and a
    // counterattack are the battler whose script place the call read (a counterattack that ran
    // already reads only its battler's bit), and a counterattack answers the targets it read. The
    // actions compared are the ones the call added to the battler's lists. A command is the action a
    // script's step can hold whose bytes are the ones the call read, and the battler is each one the
    // cartridge's own command, given the same reads, makes the same writes for. SetStatus and
    // ClearStatus make an attack, which the trace follows through the attack's own calls.
    //
    // A step of a won battle rebuilds as its RewardScenario the same way: the record is the one whose
    // cells the call touched, the spells the ones the call's spell-list place names, the abilities
    // the actor and level logged at its entry, the rages the monsters it read. The messages the call
    // showed are compared with the port's.
    [[nodiscard]] std::string replay(const TracedCall& call);

    // Replay one ending the trace logged against the port: the cells it found are written, read back
    // as an EndingScenario and played out by the port's ending of the same name (a loss handed the
    // message logged where its caller said, otherwise the one its caller hands: Banon fell from his
    // own ending, "Annihilated" from the end check, or none in the colosseum); what the port leaves is compared
    // with the cells the ending left, and the messages it shows with the ones the ending showed, in
    // order. Returns every difference, one per line; empty when they agree. Throws
    // std::runtime_error for an ending it cannot rebuild — one with an equipment change pending —
    // naming why.
    [[nodiscard]] std::string replayEnding(const TracedEnding& ending);

    // The battle at its end as the cells now hold it, read the way writeEndingScenario() writes one.
    [[nodiscard]] EndingScenario readEndingScenario();

    // Log every call the oracle's routines make to the traced routines in the trace's own lines; a
    // call the oracle makes itself is closed as the attack frame's.
    void trace(bool on) { tracing_ = on; }
    [[nodiscard]] std::vector<std::string> takeTrace() { return std::exchange(traced_, {}); }

    // Where the machine is parked: the first instructions of the game's reset code, in native mode.
    [[nodiscard]] bool atReset() const { return reset_; }

    // The machine, parked.
    [[nodiscard]] retropp::Vm& machine() { return machine_; }

    // The cells, for callers that read them back.
    static constexpr std::uint32_t kWorkRam = cells::kWorkRam;
    static constexpr std::uint32_t kRecord = kWorkRam + 0x11A0;  // the attack's 14-byte record
    static constexpr std::uint32_t kPower = kWorkRam + 0x11A6;   // the record's power
    static constexpr std::uint32_t kAttackerStat = kWorkRam + 0x11AE;
    static constexpr std::uint32_t kAttackerLevel = kWorkRam + 0x11AF;
    static constexpr std::uint32_t kDamage = kWorkRam + 0x11B0;
    static constexpr std::uint32_t kAttackFlags = kWorkRam + 0x00B3;  // the second attack-flags byte
    static constexpr std::uint32_t kMultiplier = kWorkRam + 0x00BC;
    // Per battler, a word at twice the battler's index.
    static constexpr std::uint32_t kRelicEffects1And3 = cells::kRelicEffects1And3;
    static constexpr std::uint32_t kRelicEffects4And5 = cells::kRelicEffects4And5;

private:
    using Call = void(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    using Multiply = std::uint16_t(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    using TargetCall = void(std::uint16_t, std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    using PassCall = void(std::uint8_t, std::uint8_t, std::uint16_t);

    // Run `routine` with the watches recording.
    void call(const std::function<void()>& routine);

    void writeScenario(const DamageScenario& scenario);
    void writeTargetCells(const DamageScenario& scenario);
    void writeApplyCells(const DamageScenario& scenario);
    void writeAimCells(const DamageScenario& scenario);
    void writeContext(const DamageScenario& scenario);
    void writeStatusCells(const DamageScenario& scenario);
    void writeEffectCells(const DamageScenario& scenario);
    void writeTargetEffectCells(const DamageScenario& scenario);
    void writeAttackCells(const DamageScenario& scenario);
    void writeCommandStart();
    [[nodiscard]] AttackResult readAttackResult(bool attackCells);
    [[nodiscard]] ApplyDamageResult readApplyCells();
    void registerGate();
    void noteGuestAccess(std::uint32_t cell, std::uint8_t value, bool write);
    // A whole battle's access to a cell past $7E:3FFF, for the widened read-set gate.
    void noteAccessBeyond(std::uint32_t cell, std::uint8_t value, bool write);
    void writeBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes);
    [[nodiscard]] AttackerEffectResult readAttackerEffectResult();
    [[nodiscard]] TargetEffectResult readTargetEffectResult();
    [[nodiscard]] StatusChangesResult readStatusChangesResult();
    [[nodiscard]] StatusEffectResult readStatusEffectResult(battle::BattlerSlot target);
    [[nodiscard]] DamageScenario readStatusScenario();
    [[nodiscard]] std::string replayStatusCall(const TracedCall& call, const TracedRoutine& routine);
    [[nodiscard]] DamageScenario readEffectScenario();
    [[nodiscard]] std::string replayEffectCall(const TracedCall& call, const TracedRoutine& routine, bool bounce);
    void writeRecordScenario(const RecordScenario& scenario);
    [[nodiscard]] LevelUpResult readLevelUpResult(std::size_t recordIndex);
    [[nodiscard]] SpellLearningResult readSpellLearningResult(CharacterPropId actor);
    [[nodiscard]] std::string replayRecordCall(const TracedCall& call, const TracedRoutine& routine);
    void writeParty(const PartyCells& cells);
    [[nodiscard]] PartyCells readParty();
    void writeRewardScenario(const RewardScenario& scenario);
    [[nodiscard]] RewardResult readRewardResult();
    [[nodiscard]] RewardResult callReward(const std::function<void()>& routine);
    [[nodiscard]] RewardScenario readRewardScenario();
    [[nodiscard]] std::string replayRewardCall(const TracedCall& call, const TracedRoutine& routine);
    void answerGraphics(const GraphicsCall& call);
    // What the game keeps from a battle as the party's cells now hold it (after UpdateSRAM).
    [[nodiscard]] WriteBackResult readWriteBack();
    // What the graphics program does for the battle program in a whole battle, beyond answering.
    void answerInBattle(const GraphicsCall& call);
    // What the opening ($00) lays into the battle's cells for the battle program to read.
    void layTheOpening();
    // A whole battle's answer: the frames its command takes, and at a loop step's frame wait the step's
    // cells read.
    void answerInWholeBattle(const GraphicsCall& call);
    // One frame of a whole battle, as the interrupt's body and the frame wait run it.
    void runFrame(Frame frame);
    // The loop's frame wait is next: its graphics call reads the step's cells.
    void reachedTheFrameWait();
    // A whole battle, stepped or in one call, from the cells as they stand.
    [[nodiscard]] WholeBattleResult runWholeBattle(const PresentationModel& model, std::uint64_t frameCap,
                                                   bool inOneCall);
    // The loop's head reached, or TerminateBattle's return, with the stack pointer found there.
    void stepEnded(std::uint16_t stackPointer, bool closed);
    void writeItemsObtained(const battle::ItemsObtained& list);
    [[nodiscard]] battle::ItemsObtained readItemsObtained();
    // The items-obtained list's last three places, which a battle's start leaves as they are
    // (BattleStart::itemsObtainedTail).
    void writeItemsObtainedTail(const battle::ItemsObtainedTail& tail);
    [[nodiscard]] battle::ItemsObtainedTail readItemsObtainedTail();
    void writeInventory(const std::array<battle::BattleItem, game::kInventorySlots>& inventory);
    [[nodiscard]] std::array<battle::BattleItem, game::kInventorySlots> readInventory();
    [[nodiscard]] ItemsObtainedResult callItemsObtained(const ItemsObtainedScenario& scenario, std::uint8_t command);
    void writeEndingScenario(const EndingScenario& scenario);
    [[nodiscard]] EndingResult readEndingResult();
    [[nodiscard]] EndingResult callEnding(const EndingScenario& scenario, const SnapshotRoutine& snapshot,
                                          const std::function<void()>& routine);
    void writeAiCells(const AiScenario& scenario);
    void writeAiScriptCells(const battle::BattleState& state);
    void writeAiRunCells(const AiScenario& scenario, bool counter);
    void emptyActionLists(battle::BattlerSlot actor);
    [[nodiscard]] AiScriptPlaces readScriptPlaces(battle::BattlerSlot slot);
    [[nodiscard]] AiScenario readAiScenario(battle::BattlerSlot actor);
    [[nodiscard]] std::string replayAiCall(const TracedCall& call, const TracedRoutine& routine);
    [[nodiscard]] AiRunResult readAiRunResult(battle::BattlerSlot actor);
    [[nodiscard]] AiCommandResult readAiCommandResult();
    [[nodiscard]] std::optional<battle::BattlerSlot> readTargetingTarget(battle::BattlerSlot actor);
    [[nodiscard]] DamageScenario readScenario();
    [[nodiscard]] TargetDamageResult readTargetResult();
    [[nodiscard]] ApplyDamageResult readApplyResult(std::size_t firstNumeralCommand = 0,
                                                    std::uint32_t firstNumeralBuffer = 0);
    [[nodiscard]] ReflectedAttackResult readReflectResult();
    [[nodiscard]] std::vector<battle::DamageNumbersShown> readNumerals(std::size_t firstCommand,
                                                                       std::uint32_t firstBuffer);
    void writeByte(std::uint32_t at, std::uint8_t value);
    void writeWord(std::uint32_t at, std::uint16_t value);
    // The writes of a scenario's attack, command, script or message: laid on the machine as every
    // other write is, and left out of what writeBattleState() lays.
    void writeInFlightBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes);
    void writeInFlightByte(std::uint32_t at, std::uint8_t value);
    void writeInFlightWord(std::uint32_t at, std::uint16_t value);
    // What a write lays: the state, something in flight, or the state as the cartridge holds it where
    // an existing writer's encoding differs (which overrules every other writer of the cell).
    enum class CellWrite : std::uint8_t { State, InFlight, AsTheCartridgeHoldsIt };
    void writeCells(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes, CellWrite kind);
    // writeBattleState()'s last pass: the cells where the cartridge's encoding of the state is not the
    // existing writers', laid as the cartridge holds them, and the cells no field determines left out.
    void writeStateAsTheCartridgeHoldsIt(const battle::BattleState& state);
    // The cells of that pass no routine's scenario writes: the party's command menus, spell lists and
    // skills; each battler's clocks and counters; the battle's flags, lines, records and memory.
    void layMenusAsTheCartridgeHoldsIt(const battle::BattleState& state);
    void layBattlersAsTheCartridgeHoldsIt(const battle::BattleState& state);
    void layBattleAsTheCartridgeHoldsIt(const battle::BattleState& state);
    // The battle's one list of actions: the interrupting actions, and each battler's turn actions and
    // counterattacks (oracle/battle/state_actions.cpp states where each list's slots go).
    void layActionsAsTheCartridgeHoldsIt(const battle::BattleState& state);
    // One line, laid from the start the machine holds at `startCell`: its slots in `ring` from there,
    // and its end in the byte after the start.
    void layLine(std::uint32_t startCell, std::uint32_t ring, const battle::BattlerQueue& line);
    // A ring's position as the machine holds it: the cartridge's cells a comparison is made against,
    // or the machine the state is written into.
    [[nodiscard]] std::uint8_t positionByte(std::uint32_t at);
    void layAsTheCartridgeHoldsIt(std::uint32_t at, std::span<const std::uint8_t> bytes);
    void layByteAsTheCartridgeHoldsIt(std::uint32_t at, std::uint8_t value);
    void layWordAsTheCartridgeHoldsIt(std::uint32_t at, std::uint16_t value);
    // Leave `count` cells from `at` out of what writeBattleState() lays: no field determines them.
    void leaveOut(std::uint32_t at, std::uint32_t count);
    // The cells writeBattleState() would lay `state` into, with the cells two writers lay differently,
    // with nothing written to the machine.
    struct LaidState {
        std::map<std::uint32_t, std::uint8_t> cells;
        std::map<std::uint32_t, std::uint8_t> disagreements;  // the bits two writers lay differently
        std::set<std::uint32_t> leftOut;                      // the cells no field determines
        // The rings' positions: where a line starts and where the next record is read. Each is the
        // machine's own, and what follows it is laid from it, so the comparison holds it.
        std::set<std::uint32_t> positions;
        // Cells left out whose value a later read carries to nothing the battle holds, for the state laid:
        // the carried-read gate passes a read of one.
        std::set<std::uint32_t> carriedToNothing;
    };
    // Hold `count` cells from `at` as a ring's position (LaidState::positions).
    void holdPosition(std::uint32_t at, std::uint32_t count);
    // Leave `count` cells from `at` out, as cells a later read carries to nothing
    // (LaidState::carriedToNothing).
    void leaveOutAsCarriedToNothing(std::uint32_t at, std::uint32_t count);
    // `cartridge`, where given, holds the ring positions the lines and records are laid from.
    [[nodiscard]] LaidState layState(const battle::BattleState& state, const PartyCells& boosts,
                                     const BattleCells* cartridge = nullptr);
    [[nodiscard]] std::uint8_t readByte(std::uint32_t at);
    [[nodiscard]] std::uint16_t readWord(std::uint32_t at);

    retropp::Vm::SNES machine_;
    bool reset_ = false;
    bool recording_ = false;
    std::vector<WatchedWrite> writes_;
    bool tracing_ = false;
    std::vector<std::string> traced_;
    std::unique_ptr<DamageTrace> trace_;
    retropp::Routine<Call> calcMagicDmg_;
    retropp::Routine<Call> calcDmg_;
    retropp::Routine<Call> relicDmgEffect_;
    retropp::Routine<Multiply> applyDmgMult_;
    retropp::Routine<TargetCall> calcTargetDmg_;
    retropp::Routine<Call> applyAndShowDmg_;
    retropp::Routine<TargetCall> calcReflectDmg_;
    retropp::Routine<PassCall> updateStatus_;
    retropp::Routine<TargetCall> magicStatusEffect_;
    retropp::Routine<Call> doAttackerEffect_;
    retropp::Routine<TargetCall> doTargetEffect_;
    retropp::Routine<Call> initGfxParams_;
    retropp::Routine<Call> coverEffect_;
    retropp::Routine<TargetCall> learnGenjuMagic_;
    retropp::Routine<Call> doLevelUp_;
    using TargetsCall = std::uint8_t(std::uint8_t, std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    using ConditionCall = std::uint8_t(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    retropp::Routine<TargetsCall> checkAiTarget_;
    // The condition routines by the game's numbering, each where the cartridge's table points.
    std::vector<retropp::Routine<ConditionCall>> aiConditions_;
    using SetupCall = void(std::uint16_t, std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    retropp::Routine<SetupCall> initAi_;
    retropp::Routine<Call> execMonsterAction_;
    retropp::Routine<Call> execAiRetal_;
    // The three commands, by the command number less 0x2E.
    std::array<retropp::Routine<Call>, 3> aiCommands_;
    // Damage over time, a spell with no caster, status timers running out and Seize's drain.
    std::array<retropp::Routine<Call>, 4> ownActions_;
    retropp::Routine<Call> execSelfAttack_;
    retropp::Routine<Call> execAttack_;
    // A win's rewards, and the two graphics commands that keep the items obtained.
    retropp::Routine<Call> addExp_;
    retropp::Routine<Call> learnItemMagic_;
    retropp::Routine<Call> checkLevelUp_;
    using AbilitiesCall = void(std::uint16_t, std::uint8_t, std::uint8_t, std::uint16_t);
    retropp::Routine<AbilitiesCall> learnAbilities_;
    retropp::Routine<Call> learnRage_;
    retropp::Routine<AbilitiesCall> execBtlGfx_;
    retropp::Routine<Call> addItemObtained_;
    retropp::Routine<Call> mergeItemsObtained_;
    // The battle's end, and the title program's laying out.
    retropp::Routine<Call> wrapUp_;
    retropp::Routine<Call> winBattle_;
    retropp::Routine<AbilitiesCall> loseBattle_;
    retropp::Routine<Call> updateSram_;
    retropp::Routine<Call> decompCutsceneProg_;
    // The battle program's entry, called as the field calls it (a JSL, returning by RTL).
    retropp::Routine<Call> battleMain_;
    // A whole battle a step at a time: the start's two routines, the loop, and the clock the graphics
    // program calls.
    retropp::Routine<Call> initRam_;
    retropp::Routine<Call> initBattle_;
    retropp::Routine<Call> battleLoop_;
    retropp::Routine<Call> updateBattleTime_;
    // A whole battle being run (wholeBattle()): its model, what it has made so far, and the step under way.
    struct WholeBattleRun {
        PresentationModel model;
        std::uint64_t frameCap = 0;
        WholeBattleResult result;
        CartridgeStep pending;        // the writes and reads since the last frame wait
        bool inOneCall = false;       // the battle is one call of Battle_ext, not a call per step
        bool atFrameWait = false;     // the next graphics call is the loop's frame wait
        bool stepEnded = false;       // this step came back to the loop's head
        bool closed = false;          // TerminateBattle was reached
        std::size_t scripts = 0;      // the scripts played so far
    };
    std::optional<WholeBattleRun> whole_;
    // A whole battle's last accesses by the battle program, a ring, for naming the reader of a carried read.
    struct RecentAccess {
        std::uint32_t cell = 0;
        std::uint8_t value = 0;
        bool write = false;
    };
    static constexpr std::size_t kRecentAccesses = 24;
    std::array<RecentAccess, kRecentAccesses> recentAccesses_{};
    std::size_t recentNext_ = 0;
    // A whole battle's two stack pages ($7E:0100-01FF, then $7E:1500-15FF), read once as the battle starts
    // on the parked machine and kept from the guest's writes after (a read of the running machine answers a
    // tick old): the returns on them name the routine making an access.
    static constexpr std::size_t kStackPagesSize = 0x200;
    std::array<std::uint8_t, kStackPagesSize> stackPages_{};
    void keepTheStackPages();
    std::vector<GraphicsCall> graphicsCalls_;
    // A graphics command the oracle has no answer for was called (the menu queue's position, $0D).
    std::optional<std::uint8_t> unansweredGraphics_;
    // A start being run (battleStart()): what it has reached, and the cells read at the loop's first
    // frame wait.
    std::optional<StartResult> start_;
    // While a graphics command's answer runs: its accesses are the graphics program's, not the battle
    // program's.
    bool answering_ = false;
    // The cells written since the loop's first frame wait, for the carried-read gate.
    std::set<std::uint32_t> writtenSincePoint_;
    // The cells beyond $7E:3FFF the battle program wrote in the battle being run.
    std::set<std::uint32_t> writtenBeyond_;
    // Whether the watch past the battle's own cells is armed. A whole battle arms it and leaves it
    // armed, so battles called one after another arm it once; any other call disarms it first, so it
    // runs as fast as it did. Arming and disarming it each walk the machine's every watched page.
    bool beyondArmed_ = false;
    const MonsterSprites* monsterSprites_ = nullptr;
    const TextCorpus* text_ = nullptr;
    // While layState() lays a state out: the cells laid, instead of writing the machine.
    LaidState* capture_ = nullptr;
    const BattleCells* positionsFrom_ = nullptr;  // while layState() captures against a cartridge
    // The read-set gate: the cells written since the last call began (by the oracle before it, by the
    // guest during it), and the cells the guest read during the last call before any was written.
    std::set<std::uint32_t> gateWritten_;
    std::map<std::uint32_t, std::uint8_t> unwrittenReads_;
    bool gateCalling_ = false;
    bool gateFresh_ = true;
};

// Run `work` from the one tick of a run loop on the console's cadence, with no window, stepped as
// fast as the host allows (TickClock). The run ends through the loop's exit: the tick asks for it
// and the exit action lets it proceed. Whatever `work` throws comes out of here.
void runOnLoop(const std::function<void()>& work);

}  // namespace ostinato::trace
