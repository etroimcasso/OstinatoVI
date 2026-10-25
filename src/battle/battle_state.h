// The state of a battle in progress: the ten battlers and everything the battle tracks between
// frames.
//
// This is logic only — nothing here draws. The presentation reads it (HP, the ATB gauges, who is
// acting) and drains BattleState::requests; the battle advances it with tickBattleTime() and
// stepBattle() (battle/battle_core.h).
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_core.md.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>
#include <vector>

#include "battle/action_queue.h"
#include "battle/battle_requests.h"
#include "battle/battle_rng.h"
#include "battle/battler_set.h"
#include "ostinato/character_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/game_version.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

// The ATB gauge: how close a battler is to its next turn.
//
// The gauge fills by the battler's ATB rate on every time update. Its high byte is the meter: 1
// is empty, it climbs to 255, and when it passes 255 it reads 0 — full. The low byte is the part
// of an update the gauge has gained toward the next step of the meter; it is kept when the gauge
// fills, so a battler's next turn starts slightly ahead.
struct AtbGauge {
    std::uint16_t counter = 0xFFFF;

    constexpr std::uint8_t meter() const { return static_cast<std::uint8_t>(counter >> 8); }

    constexpr bool isFull() const { return meter() == 0; }

    // How full the gauge looks: 0 (empty) to 255 (full).
    constexpr std::uint8_t fillLevel() const { return static_cast<std::uint8_t>(meter() - 1); }

    // Add one update's fill. Returns true when this fills the gauge, which then reads full.
    constexpr bool fill(std::uint16_t rate) {
        const unsigned sum = static_cast<unsigned>(counter) + rate;
        counter = static_cast<std::uint16_t>(sum);
        if (sum <= 0xFFFF) {
            return false;
        }
        setMeter(0);
        return true;
    }

    // Empty the gauge, keeping the part-step.
    constexpr void empty() { setMeter(1); }

    // Make a full gauge fill again at its next update.
    constexpr void setNearlyFull() { setMeter(0xFF); }

    // After a turn, a full gauge starts again from empty and any other gauge moves one step back,
    // unless it is one step from full.
    constexpr void stepBackAfterTurn() {
        if (meter() != 0xFF) {
            setMeter(static_cast<std::uint8_t>(meter() + 1));
        }
    }

    constexpr void setMeter(std::uint8_t meter) {
        counter = static_cast<std::uint16_t>((meter << 8) | (counter & 0x00FF));
    }
};

// What has to be worked out again for a battler once its action has finished. Each is done once,
// in this order, and cleared.
struct PendingUpdates {
    bool leaveTheField = false;      // Palidor carries the battler off (Jump-like flight)
    bool enabledMagic = false;       // which spells it can cast now
    bool removeAllActions = false;   // drop everything it had queued
    bool startCondemned = false;     // a condemned count starts
    bool stopCondemned = false;      // the condemned count stops
    bool commandList = false;        // its battle commands changed
    bool speed = false;              // its ATB rate
    bool morph = false;              // its menu after Morph or Revert
};

// Per-battler conditions the battle checks as it goes.
struct BattlerTriggers {
    bool runic = false;         // Runic is up: the next spell is absorbed
    bool retort = false;        // Retort is up: the next physical hit is countered
    bool regenDue = false;      // the damage-over-time timer for regen and sap fired
    bool poisonDue = false;     // the damage-over-time timer for poison fired
    bool piranha = false;       // dies without waiting for its counterattack
    bool usesControlMenu = false;  // controlling a monster: its menu shows the monster's attacks
    bool overcast = false;
    bool phantasm = false;      // Phantasm drains it like Sap
};

// A spell a party member can cast in battle, and the MP it costs that member.
struct SpellCost {
    AttackId spell{};
    std::uint8_t mpCost = 0;
};

// One of the ten battlers.
struct Battler {
    // In the battle and on the field.
    bool present = false;

    // --- whose turn it is --------------------------------------------------------------------
    bool takesMenuInput = false;     // its commands are chosen in the battle menu
    bool inBattleMenu = false;       // its menu is open
    bool atbStopped = false;         // its gauge is not filling: full, or frozen by an effect
    bool heldByStatus = false;       // dead, petrified or stopped: cannot take a turn
    bool actionQueued = false;       // its action is ready and waiting to run
    bool heldByControl = false;      // controlled, asleep, frozen or seized: cannot take a turn
    bool menuMayOpen = false;        // its menu opens when its gauge is full
    bool skipAdvanceWait = false;    // its next pending action is ready at once
    bool defending = false;          // Def
    bool dotActionPending = false;   // a damage-over-time action is waiting to run
    bool emptyGaugeAfterAction = false;  // its gauge empties once the current action ends
    bool requeuePending = false;     // its pending actions go back in line when it can act again

    PendingUpdates pendingUpdates;

    bool nearFatalSpellAvailable = true;  // its near-fatal relic spells can still be cast this battle
    bool airAnchored = false;             // Air Anchor: it dies on landing
    bool mayActAfterJump = true;          // on the ground: its menu may open and it may leap for a Jump

    // --- the clocks --------------------------------------------------------------------------
    AtbGauge gauge;
    std::uint16_t atbRate = 0;            // how much the gauge fills per time update

    // How long the battler waits between choosing its action and taking it, in advance-wait
    // steps; unset until its action is chosen, which waits as long as a wait can.
    std::optional<std::uint8_t> advanceWait;
    std::uint16_t advanceWaitCounter = 0;  // the advance wait so far, in the same steps

    // The status timers. They advance by statusTimerStep (larger under Haste, smaller under Slow)
    // once every 16 time updates; each time the sum passes 255 the timers below take one step.
    std::uint8_t statusTimer = 0;
    std::uint8_t statusTimerStep = 0;
    std::uint8_t stopTimer = 0;            // Stop wears off when it reaches 0
    std::uint8_t reflectTimer = 0;
    std::uint8_t freezeTimer = 0;
    std::uint8_t sleepTimer = 0;
    std::uint8_t condemnedNumber = 0;      // the countdown shown over a condemned battler; 0 = none
    std::uint8_t damageOverTimePhase = 0;  // which beat of the poison/regen rhythm it is on
    std::uint8_t morphGauge = 0;           // the Morph meter shown

    // How far its own clock has run: once every 16 time updates while it is alive and present.
    std::uint16_t turnTimer = 0;

    // Getting away: the progress the character has made, and the most one try can add.
    int runProgress = 0;
    std::uint8_t runFactor = 0;

    // --- who and what it is ------------------------------------------------------------------
    std::optional<CharacterId> actor;     // a party member: who it is
    std::uint16_t hp = 0;
    std::uint8_t speed = 0;
    StatusSet status;
    BattlerTriggers triggers;
    bool actsAsEnemy = false;             // a party member fighting against the party
    bool aiTakesTurns = false;            // its actions come from an AI script
    bool hasCounterScript = false;        // its AI answers attacks
    bool firstStrike = false;             // a monster that acts at once when it appears
    bool harderToRunFrom = false;         // a monster that makes escaping slower
    bool preventsEscape = false;          // a monster the party cannot run from
    bool blackBelt = false;               // wears a Black Belt: may answer a monster's hit
    std::vector<SpellCost> spellCosts;    // a party member's castable spells

    // --- links to other battlers --------------------------------------------------------------
    std::optional<BattlerSlot> controlledBy;
    std::optional<BattlerSlot> charmedBy;
    std::optional<BattlerSlot> seizing;   // the battler it holds in Seize
    std::optional<BattlerSlot> seizedBy;
    std::optional<BattlerSlot> lastAttacker;  // the last battler to target it
    bool retaliationPending = false;          // it was hit and may counter lastAttacker

    // --- what it has waiting ------------------------------------------------------------------
    std::deque<Action> actions;         // in the order they were chosen
    std::deque<Action> counterActions;  // counterattacks, in the order they were triggered
};

// The four ways a battle can be laid out.
enum class BattleType : std::uint8_t {
    Front,
    Back,
    Pincer,
    Side,
};

// The player's battle settings.
struct BattleConfig {
    bool waitMode = true;           // time stops while a command's sub-menu is open
    std::uint8_t speedSetting = 2;  // 0 (fastest) to 5, as the config menu shows it 1-6
    bool gaugeShown = true;

    // How much the speed setting slows the monsters: 255 at the fastest, 24 less for each step.
    constexpr std::uint8_t monsterSpeedFactor() const {
        return static_cast<std::uint8_t>(255 - 24 * speedSetting);
    }
};

// The events that end a battle in a scripted way, set by an attack or by the battle itself.
enum class EndOfBattleEvent : std::uint8_t {
    RanOutOfTime = 1,    // also how Warp and a full sneeze-out end a battle
    GauLeapt,
    BanonFell,
    EndNow,
    RagesLearned,
    FinalBattleNextStage,
};

// Things the last command's execution reports for the counterattack checks. Reset before every
// command.
struct ActionScratch {
    bool countersEnabled = true;                    // something in the action can disable them
    bool physical = false;                          // the attack was a physical hit
    std::optional<BattlerSlot> interceptorGuarding; // the character Interceptor stepped in for
    BattlerSet blackBeltEligible;                   // characters a Black Belt may counter for
    BattlerSet finalTargets;                        // who the action ended up hitting
};

// What the battle remembers for Mimic.
struct MimicMemory {
    CommandAction last;                       // the last command a character used
    std::optional<CommandAction> secondSpell; // its X-Magic second spell, if it was X-Magic
    bool replayJump = false;                  // the last thing was a Jump: Mimic jumps too
    BattlerSet jumpTargets;
};

// How a battle ended without being won or lost.
enum class BattleEndReason : std::uint8_t {
    RanOutOfTime,          // the story's time ran out; also Warp, and every character sneezed away
    GauLeapt,
    EndNow,
    RagesLearned,
    FinalBattleNextStage,  // the final battle moves on to its next stage
    TimerExpired,          // a story countdown reached zero
    EatenByZoneEater,      // Zone Eater swallowed the whole party
};

struct BattleWon {};

// The party fell. `message` is the battle message shown, if any.
struct BattleLost {
    std::optional<std::uint8_t> message;
};

struct BattleEnded {
    BattleEndReason reason = BattleEndReason::EndNow;
    std::optional<FormationId> nextFormation;  // the final battle's next stage
};

using BattleOutcome = std::variant<BattleWon, BattleLost, BattleEnded>;

// The battle messages a battle's end shows.
inline constexpr std::uint8_t kMessageAnnihilated = 41;
inline constexpr std::uint8_t kMessageBanonFell = 54;

// Where the battle loop stopped to wait for a battle script.
enum class ScriptResume : std::uint8_t {
    NextStep,         // nothing left to do but carry on
    FinishAction,     // the rest of a battler's turn
    FinishCounter,    // the rest of a counterattack
    FinishImmediate,  // the rest of an action that interrupted the line
};

struct ScriptWait {
    ScriptResume resume = ScriptResume::NextStep;
    std::optional<BattlerSlot> actor;
    std::optional<CommandAction> command;  // the command that ran, for Mimic to remember
};

// The whole of a battle in progress.
struct BattleState {
    std::array<Battler, kBattlerSlots> battlers{};

    Battler& at(BattlerSlot slot) { return battlers[static_cast<std::size_t>(slot.index())]; }
    const Battler& at(BattlerSlot slot) const { return battlers[static_cast<std::size_t>(slot.index())]; }

    GameVersion version = GameVersion::US_1_1;
    BattleConfig config;
    BattleRng rng;

    // --- the lines --------------------------------------------------------------------------
    BattlerQueue advanceWaitQueue;  // waiting out the pause before acting
    BattlerQueue actionQueue;       // ready to act
    BattlerQueue counterQueue;      // a counterattack to make
    std::deque<Action> immediateActions;  // jump the line: run before anything else

    std::optional<BattlerSlot> actingBattler;     // mid-turn with more actions to take
    std::optional<BattlerSlot> counteringBattler; // mid-counterattack with more to make

    // Quick: the battler taking extra turns, and how many it has left. A count that has just run
    // out reads 0 until the next loop step clears it, and Quick cannot be cast until then.
    std::optional<BattlerSlot> quickTarget;
    std::optional<std::uint8_t> quickTurnsLeft;

    // --- the clocks --------------------------------------------------------------------------
    int framesSinceTimeUpdate = 0;  // frames since the ATB last advanced
    bool timeStoppedByMenu = false; // a command's sub-menu is open (stops time in wait mode)
    std::uint16_t turnCounter = 0;  // time updates so far
    std::uint16_t battleCounter = 0;  // once every 16 time updates
    std::uint8_t counterPhase = 0;  // which of the 16 timer jobs the next time update runs

    // --- who is where ------------------------------------------------------------------------
    BattlerSet alive;              // the party members and monsters still fighting
    BattlerSet aliveEnemyCharacters;  // party members still fighting, against the party
    int allyCount = 0;             // party members fighting for the party
    int enemyCount = 0;            // monsters and party members fighting against it
    BattlerSet targetableOrPresent;
    BattlerSet joiningAfterAction; // will be on the field once the current action is over
    BattlerSet leavingAfterAction; // will be gone once the current action is over
    BattlerSet countedInBattle = BattlerSet::allCharacters() | BattlerSet::allMonsters();
    BattlerSet monstersStanding;   // monsters still standing on the field
    BattlerSet monstersFallen;     // monsters that have died or fled
    BattlerSet partyLeft;          // characters who have left the battle (escaped, sneezed away)
    BattlerSet partyJustEscaped;   // characters whose escape has just succeeded
    BattlerSet engulfed;           // characters Zone Eater has swallowed
    BattlerSet partyPresentAtStart;
    BattlerSet awayFromTheField;   // jumping, carried off by Palidor, or held in Seize
    BattlerSet statusChanged;      // statuses changed this action: their next command re-aims
    BattlerSet menusToRefresh;
    BattlerSet diedAwaitingCounter;  // died this action and may answer it from their AI
    BattlerSet attackedThisStep;   // were attacked by another battler during this loop step
    BattlerSet counterAiRanThisStep;
    std::array<BattleSlotMask, 2> pincerSides{};  // the monsters on each side of a pincer attack
    std::optional<BattlerSlot> heldByZinger;      // the monster Zinger has taken off the field
    std::optional<BattlerSlot> morphedBattler;    // the character in Morph
    std::optional<BattlerSlot> rouletteTarget;    // Roulette's choice, struck by Doom on its beat

    // --- the battle's flags ------------------------------------------------------------------
    bool runActionQueued = false;   // the party-escape action is already waiting
    bool monsterListChanged = false;
    bool preemptive = false;
    bool actorStillActing = false;  // the current battler has more of its turn to take
    bool counterattackRunning = false;
    bool cannotRun = false;
    bool escapeBlockedByMonster = false;
    bool backAttack = false;
    bool mimicking = false;         // the next action created is Mimic's: it costs no MP
    bool menusDisabled = false;
    bool endTurnAfterAction = false;  // Sonic Dive: the battler's turn ends with this action
    bool battleMayNotEnd = false;   // a hidden monster is about to reappear
    bool partyRunning = false;      // the player is holding the run buttons
    std::uint8_t runDifficulty = 0;
    bool colosseum = false;
    BattleType battleType = BattleType::Front;
    FormationId formation{};
    bool finalBattleChain = false;
    std::optional<EndOfBattleEvent> endOfBattleEvent;
    std::uint8_t condemnedLevel = 20;  // the level a new condemned count is worked out from

    // Facts about the party and the story the battle's end depends on.
    bool gauMayAppear = false;      // on the Veldt: Gau can turn up when the battle is won
    bool gauRecruited = false;
    bool shadowStays = false;       // Shadow will not walk off after this battle
    bool shadowAvailable = false;
    bool eventTimerExpired = false; // a story countdown ran out during the battle

    // What the battle's end changed: Gau has just turned up, Shadow has walked off.
    bool gauJoining = false;
    bool shadowLeftParty = false;
    bool noVictoryFanfare = false;

    std::optional<BattleOutcome> outcome;

    ActionScratch scratch;
    MimicMemory mimic;

    // Requests for the presentation, in order. The host drains them every frame.
    std::vector<BattleRequest> requests;

    // Set while the battle loop waits for a battle script to finish.
    std::optional<ScriptWait> waitingForScript;
};

}  // namespace ostinato::battle
