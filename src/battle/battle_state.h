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
#include <bitset>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>
#include <vector>

#include "battle/action_queue.h"
#include "battle/battle_inventory.h"
#include "battle/battle_requests.h"
#include "battle/battle_rng.h"
#include "battle/battler_set.h"
#include "battle/player_intake.h"
#include "battle/spell_lists.h"
#include "data/item_properties.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_gfx_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/dance_id.h"
#include "ostinato/element_set.h"
#include "ostinato/equip_permissions.h"
#include "ostinato/esper_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/metamorph_info.h"
#include "ostinato/monster_attack_animation.h"
#include "ostinato/monster_flags.h"
#include "ostinato/monster_id.h"
#include "ostinato/monster_special_attack.h"
#include "ostinato/status_set.h"
#include "ostinato/targeting.h"

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
    bool monsterRunic = false;  // a monster's own Runic: it absorbs the spells cast at it
};

// What a party member holds in one hand, as the battle uses it to attack. The weapon hand is the
// record's weapon slot and the shield hand its shield slot; either can hold a weapon.
struct Hand {
    ItemId item = ItemId::EMPTY;
    std::uint8_t battlePower = 0;     // the character's own plus the weapon's; 0 for a hand that does not attack
    std::uint8_t hitRate = 0;
    ElementSet element;
    ItemSpellCast spellCast;
    WeaponFlagSet weaponFlags;        // Two-hand is set only while a Gauntlet is worn
    ItemSpecialEffect blockEffect;    // the item's block and weapon-effect byte
};

// The block animations a party member's equipment can show, one per BlockGraphic.
struct BlockGraphicSet {
    std::uint8_t bits = 0;

    constexpr bool has(BlockGraphic graphic) const {
        return (bits & (1u << static_cast<std::uint8_t>(graphic))) != 0;
    }
    constexpr void add(BlockGraphic graphic) {
        bits = static_cast<std::uint8_t>(bits | (1u << static_cast<std::uint8_t>(graphic)));
    }
};

// One row of a party member's battle command menu. A row whose command was taken away when the
// battle started holds NONE.
struct MenuCommand {
    BattleCommandId command = BattleCommandId::NONE;
    bool usable = false;  // shown in white rather than gray
    Targeting targeting;
};

// Every status, for a battler that can be given any of them.
inline constexpr StatusSet kAllStatuses{{0xFF, 0xFF, 0xFF, 0xFF}};

// The last time a battler was hit with something: who did it, and with what.
template <typename T>
struct Hit {
    BattlerSlot by = BattlerSlot::character(0);
    T with{};
};

// What a battler was last hit with. Monster AI conditions read them ("if hit by Fire").
struct LastHits {
    std::optional<Hit<BattleCommandId>> command;  // by any attack, with the command as it was chosen
    std::optional<Hit<AttackId>> spell;           // by a spell or skill
    std::optional<Hit<ItemId>> item;              // by an item
    std::optional<Hit<ElementSet>> element;       // by any attack, with its elements (none is a value)
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
    std::optional<CharacterPropId> actor;  // a party member: whose battle properties it uses
    std::optional<std::size_t> record;     // a party member: its record in BattleState::party
    CharacterGfxId graphics{};             // a party member: the sprite it is drawn with
    std::uint8_t level = 0;
    std::uint16_t hp = 0;
    std::uint16_t maxHp = 0;
    std::uint16_t mp = 0;
    std::uint16_t maxMp = 0;
    std::uint8_t speed = 0;
    std::uint8_t vigor = 0;                // a party member's is twice the character's
    std::uint8_t stamina = 0;
    std::uint8_t magicPower = 0;
    std::uint8_t defense = 0;
    std::uint8_t magicDefense = 0;

    // Evade and magic block as the hit check reads them: 256 less twice the rating, 255 for a
    // rating of 0, and 1 for a rating of 128 or more.
    std::uint8_t evadeFactor = 0;
    std::uint8_t magicBlockFactor = 0;

    StatusSet status;
    StatusSet innateStatuses;       // set on the battler when the battle starts
    StatusSet equipmentStatuses;    // statuses its equipment keeps on it
    StatusSet inflictableStatuses = kAllStatuses;  // the statuses that can be put on it
    bool instantDeathProtected = false;  // immune to instant death (equipment that blocks Dead)
    bool backRow = false;
    bool human = false;
    bool undead = false;
    BattlerTriggers triggers;
    bool actsAsEnemy = false;             // a party member fighting against the party
    bool aiTakesTurns = false;            // its actions come from an AI script
    bool hasCounterScript = false;        // its AI answers attacks
    // The AI script it follows: a monster's own (by MonsterId), or the one a scripted battle gives
    // a party member.
    std::optional<std::uint16_t> aiScript;

    // --- a monster's record -------------------------------------------------------------------
    std::optional<MonsterId> monster;     // which monster it is
    MonsterBattleFlags monsterFlags;      // first strike, harder to run from, can't run, the immunities
    bool diesAtZeroMp = false;
    bool impCritical = false;
    bool nameHidden = false;              // left out of the list of monsters' names
    // The monster whose line in the list of names it shares: monsters with the same name are
    // counted on one line.
    std::optional<MonsterId> nameGroup;
    ItemId rareSteal = ItemId::EMPTY;
    ItemId commonSteal = ItemId::EMPTY;
    std::uint16_t experience = 0;
    std::uint16_t gold = 0;
    MetamorphInfo metamorph{};
    MonsterSpecialAttack specialAttack{};
    MonsterAttackAnimation specialAttackAnimation{};

    // --- a party member's equipment -----------------------------------------------------------
    Hand weaponHand;
    Hand shieldHand;
    std::array<ItemId, 2> relics{ItemId::EMPTY, ItemId::EMPTY};
    ElementSet elementsAbsorbed;
    ElementSet elementsNullified;
    ElementSet elementsWeak;
    ElementSet elementsHalved;
    BlockGraphicSet physicalBlockGraphics;  // what blocking a physical hit can show
    BlockGraphicSet magicBlockGraphics;     // what blocking a spell can show

    // Its relic effects from all six slots. Raise-magic-damage in the third set is one item that
    // raises magic damage (Earrings, a Hero Ring); in the first set it is two of them, the doubled
    // effect. Equip-2-weapons in the fourth set stays only while a Genji Glove is worn without a
    // weapon in each hand.
    RelicEffect1Set relicEffects1;
    RelicEffect2Set relicEffects2;
    RelicEffect3Set relicEffects3;
    RelicEffect4Set relicEffects4;
    RelicEffect5Set relicEffects5;
    bool mayHoldTwoWeapons = false;       // a Genji Glove: the Item menu lets it hold two weapons

    // The equippable-by bits an item must share for this party member to hold it.
    EquipPermissions equipsAs;

    // The battle's Item menu swapped what it holds; the battle re-derives it on its next step.
    bool equipmentChanged = false;

    std::array<MenuCommand, 4> commands{};
    SpellList spells;
    int spellCount = 0;                   // spells it can use from its list, not counting lores

    // What a party member's chosen command took up: the item in its hand for Item or Throw, the
    // dance it dances and the monster it rages as. A dance or rage left empty is chosen at random
    // when it is used.
    std::optional<ItemId> itemInHand;
    std::optional<DanceId> dance;
    std::optional<MonsterId> rage;

    // --- links to other battlers --------------------------------------------------------------
    std::optional<BattlerSlot> controlledBy;
    std::optional<BattlerSlot> charmedBy;
    std::optional<BattlerSlot> seizing;   // the battler it holds in Seize
    std::optional<BattlerSlot> seizedBy;
    std::optional<BattlerSlot> lastAttacker;  // the last battler to target it
    bool retaliationPending = false;          // it was hit and may counter lastAttacker
    std::optional<BattlerSlot> bodyguard;     // Love Token: the battler who takes its hits

    // --- what the action being run does to it -------------------------------------------------
    StatusSet statusesToSet;    // put on it when the action's statuses take effect
    StatusSet statusesToClear;  // taken off it then
    LastHits lastHit;

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
    std::uint8_t magicOrder = 0;    // the spell-order setting, 0 to 5, as the menu shows it 1-6

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

    // The party the battle was started with. The battle reads and changes it as it goes; at its
    // end the caller takes it back.
    game::Party party;

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
    BattlerSet aiUntargetable;     // monsters their AI has made impossible to aim at
    // The battlers facing right. In a side or pincer attack, an attacker hits a target in the
    // back when both face the same way.
    BattlerSet facingRight;
    std::optional<BattlerSlot> morphedBattler;    // the character in Morph
    std::optional<BattlerSlot> rouletteTarget;    // Roulette's choice, struck by Doom on its beat
    std::optional<BattlerSlot> doomGaze;          // Doom Gaze, whose HP outlasts the battle
    BattlerSet possessed;          // characters Possess has taken: they leave the party

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
    std::optional<ItemId> colosseumWager;  // the item the player bet: given up win or lose
    std::optional<ItemId> colosseumPrize;  // what winning the match earns
    std::optional<MonsterId> colosseumOpponent;
    BattleType battleType = BattleType::Front;
    FormationId formation{};               // the formation being fought
    // The formation the battle began with. In the final battle it stays the first stage's.
    FormationId startingFormation{};
    BattleBackgroundId background{};
    bool keepFieldMusic = false;           // the field's music plays on through the battle
    bool finalBattleChain = false;
    // In the final battle, where the background scrolls to as a stage after the first arrives.
    std::optional<std::uint8_t> finalBattleScroll;
    std::optional<EndOfBattleEvent> endOfBattleEvent;
    std::uint8_t condemnedLevel = 20;  // the level a new condemned count is worked out from

    // The battle switches, as the battle started with them; monster AI changes them as it goes,
    // and the battle hands them back when it ends.
    game::BattleSwitches switches;

    // Facts about the party and the story the battle's end depends on.
    bool gauMayAppear = false;      // on the Veldt: Gau can turn up when the battle is won
    bool gauRecruited = false;      // Gau is one of the characters the party can field now
    bool gauObtained = false;       // Gau has joined the party at least once
    bool shadowStays = false;       // Shadow will not walk off after this battle
    bool shadowAvailable = false;
    bool eventTimerExpired = false; // a story countdown ran out during the battle

    // Facts about the story that shape the party's commands.
    bool magicOnly = false;         // only magic works here (Fanatics' Tower)
    bool morphAvailable = false;    // Terra can Morph
    bool permanentMorph = false;    // Terra stays morphed for the whole battle
    bool onTheVeldt = false;

    // The party's items and what it knows.
    BattleInventory inventory;
    RelicEffect2Set partyRelicEffects2;   // every party member's second relic effects together
    BattlerSet espersSummoned;            // party members who have summoned their esper this battle
    int swdtechsKnown = 0;
    int blitzesKnown = 0;
    int loresKnown = 0;
    std::array<std::optional<DanceId>, 8> dancesKnown{};  // by DanceId; empty where not known
    std::vector<MonsterId> ragesKnown;                    // in MonsterId order
    std::optional<DanceId> lastDance;                     // the dance last chosen from the menu

    // What the battle's actions have used up or learned so far.
    BattlerSet desperationUsed;                           // party members who have used theirs
    std::bitset<game::kLores> loresLearned;               // lores Strago saw this battle, Condemned first
    std::optional<BattlerSlot> lastRager;                 // the party member who last used Rage
    std::uint16_t golemHp = 0;                            // the Golem's HP left; 0 with no Golem

    // The commands the player has finished choosing, waiting for the battle to take them in
    // (battle/player_intake.h).
    std::deque<PlayerAction> chosenActions;

    // In the final battle, the record indices of the party members waiting to take a fallen
    // fighter's place, in the order the player chose them.
    std::deque<std::size_t> finalBattleReserve;

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
