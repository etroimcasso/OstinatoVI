// One attack as the battle carries it out: the attack in flight, the rules it is aimed by, the
// hooks the rest of the battle plugs into, and the attack loop itself.
//
// A battle command starts an AttackInFlight with startAttack(), loads the attack it makes into it
// (battle/commands.h), and hands it to runAttack(), which runs every attack of the command — one
// for most, two for a Fight (one per hand), eight with an Offering. Each attack is aimed, checked
// against each target, and written into the command's battle script.
//
//     AttackInFlight attack = startAttack(command);
//     runCommand(state, hooks, script, attack, actor);   // loads the attack, then runAttack()
//
// Everything an attack changes on the way is a field of the AttackInFlight, never of the shipped
// attack record: the record is copied in and edited there.
//
// Damage, statuses and the special effects are worked out by AttackHooks, called at the points
// where the original works them out. Its members do nothing by default, so an attack run with
// plain AttackHooks reaches its targets, hits or misses them and is shown, but changes nobody's
// HP or statuses.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_commands.md.
#pragma once

#include <cstdint>
#include <optional>

#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/attack_properties.h"
#include "data/item_properties.h"
#include "ostinato/attack_effects.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/targeting.h"

namespace ostinato::battle {

// The special effects a battle command gives its attack, in the same effect space as an attack's
// own (AttackSpecialEffect), a weapon's (WeaponSpecialEffect) and an item's (ItemUseEffect).
enum class CommandEffect : std::uint8_t {
    Possess = 0x50,
    GpRain = 0x51,
    Steal = 0x52,
    Control = 0x53,
    Leap = 0x54,
    Sketch = 0x55,
    Debilitator = 0x56,
    AirAnchor = 0x57,
};

// The special effect an attack carries, from wherever it came: the attack's own, the weapon's,
// the item's or the command's. Pummel's effect and an attack's NONE both mean no effect.
class SpecialEffect {
public:
    constexpr SpecialEffect() = default;

    static constexpr SpecialEffect of(AttackSpecialEffect effect) {
        if (effect == AttackSpecialEffect::NONE) {
            return {};
        }
        return SpecialEffect(static_cast<std::uint8_t>(effect));
    }
    static constexpr SpecialEffect of(WeaponSpecialEffect effect) {
        return SpecialEffect(static_cast<std::uint8_t>(effect));
    }
    static constexpr SpecialEffect of(ItemUseEffect effect) {
        return SpecialEffect(static_cast<std::uint8_t>(static_cast<std::uint8_t>(effect) + kItemUseBase));
    }
    static constexpr SpecialEffect of(CommandEffect effect) {
        return SpecialEffect(static_cast<std::uint8_t>(effect));
    }

    constexpr bool isNone() const { return index_ == 0; }

    // The effect's place in the effect space: an attack's effects take its own values, a weapon's
    // 1-14, an item's $49-$4E and a command's $50-$57. 0 is no effect.
    constexpr std::uint8_t index() const { return index_; }

    friend constexpr bool operator==(SpecialEffect, SpecialEffect) = default;

private:
    static constexpr std::uint8_t kItemUseBase = 0x48;

    explicit constexpr SpecialEffect(std::uint8_t index) : index_(index) {}

    std::uint8_t index_ = 0;
};

// The rules an attack is aimed by. A command starts from its own; the attack it makes and the
// command's body add to them.
struct AimingRules {
    bool noCharacterTargets = false;        // party members are never aimed at
    bool randomTarget = false;              // re-aimed at random, whoever was chosen
    bool fallBackToEarlierTargets = false;  // with no one left, the command's earlier targets
    bool reflected = false;                 // bounced off a Reflect
    bool mayTargetTheFallen = false;        // the dead and the absent stay targets
    bool keepTargetsThatAreGone = false;    // with no one left, it is aimed at no one
    bool opposingSideOnly = false;          // every target is on the other side from the attacker
    bool excludeAttacker = false;           // never aimed at the attacker
};

// The name an attack is shown under.
enum class AttackNameKind : std::uint8_t {
    Attack,               // the attack's own name
    Item,                 // the item used
    Esper,                // the esper summoned
    Bushido,              // the Bushido technique
    Command,              // the command's name
    MonsterSpecialAttack, // a monster's name for its special attack
    JokerDoom,            // Slot's Joker Doom
    Blitz,                // the blitz
};

// A sketch that caught its monster: the sketcher makes `attack` next, as `monster` would.
struct SketchDrawn {
    BattlerSlot monster = BattlerSlot::monster(0);
    AttackId attack{};
};

// What one command keeps track of between its attacks.
struct AttackScratch {
    std::optional<AttackId> followUpSpell;     // a weapon's spell, cast after the hit
    std::optional<std::uint8_t> message;       // a battle message shown after the attack
    std::optional<AttackId> lastSpell;         // the spell this command casts
    std::optional<ItemId> lastItem;            // the item this command uses
    bool itemKept = false;                     // the item used is not used up
    std::optional<AttackNameKind> nameKind;    // no name is shown without one
    std::optional<BattleCommandId> fightCommand;  // Fight or Capture: each attack loads a hand
    bool damageModification = true;            // damage varies, and defense, Shell and Safe count
    bool randomizeTargets = false;             // the next attack is re-aimed at random
    std::optional<BattlerSlot> sketcher;       // Sketch: the party member drawing the sketch
    std::optional<SketchDrawn> sketch;         // Sketch: what the sketch caught, set by its effect
    bool quietMisses = false;                  // a miss shows nothing, and no hit shows nothing
    bool hideWithoutTargets = false;           // an attack aimed at no one shows nothing
    bool aimExactly = false;                   // the next aim keeps the targets exactly as given
    std::optional<int> extraHits;              // Launcher or Super Ball: the hits still to come

    int attacksLeft = 0;                       // attacks still to run after this one
    int targetCount = 0;                       // the targets this attack was aimed at
    std::uint8_t mpCost = 0;                   // taken when the next attack is made

    BattlerSet currentTargets;                 // the targets of the attack being made
    BattlerSet earlierTargets;                 // every target the command has had
    BattlerSet hitInTheBack;
    BattlerSet missed;                         // shown "Miss"
    BattlerSet missedByEffect;                 // the special effect missed them
    std::optional<BattlerSlot> golemBlocked;   // the party member the Golem stepped in for
    std::optional<BattlerSlot> interceptorBlocked;  // the party member Interceptor stepped in for
    ItemSpellCast weaponSpellcast;             // the hand's spell, cast one time in four
    bool dragonHorn = false;                   // Dragon Horn: the jump comes down more than once
};

// One battle command's attack as it is carried out.
struct AttackInFlight {
    // The command it is shown as. Past MAGITEK, one of the special animations (SpecialAnimation).
    BattleCommandId command = BattleCommandId::FIGHT;
    BattleCommandId queuedCommand = BattleCommandId::FIGHT;  // the command as it was chosen
    AttackId queuedAttack{};                                 // the attack or item as it was chosen
    AttackId attack{};                                       // the attack being made

    AttackProperties record{};       // a copy of the attack's record, as this attack has changed it
    SpecialEffect effect;            // the special effect it carries
    std::uint8_t attackerStat = 0;   // the attacker's vigor for a physical attack, magic power for a spell
    std::uint8_t attackerLevel = 0;
    int damage = 0;                  // the damage worked out before it is split or multiplied

    AimingRules rules;
    Targeting targeting;
    BattlerSet targets;

    // The damage multiplier, in halves: each step adds half the damage again.
    int multiplier = 0;
    int extraMultiplier = 0;            // the command's own steps, where each attack's multiplier starts

    bool canBeCritical = false;
    bool automaticCritical = false;
    bool vanishApplies = true;          // a vanished target is hit by spells and missed by blows
    bool rowIgnored = true;             // the attacker's back row does not halve the damage
    bool emptyTargetsReaim = true;      // an attack left with no targets is aimed again
    bool hitsNoOne = false;             // a Blitz whose buttons were wrong
    bool firstPass = true;
    bool followUpSameTargets = false;   // the weapon's spell goes after the hit's targets
    bool nameShown = false;
    bool runicAbsorbing = false;        // Runic drew this attack in: its own animation still to show
    bool castingSwirlSuppressed = true; // the swirl before a spell does not play
    bool twoHanded = false;             // the hand's weapon is held in both hands (a Gauntlet)
    // An attack that ignores defense is not multiplied. A Throw multiplies it anyway.
    bool ignoreDefenseStopsMultiplier = true;

    std::uint8_t secondary = 0;         // the animation's second value (AttackAnimation::secondary)
    bool offHand = false;               // the hit is the shield hand's
    bool showsAttack = true;            // the animation plays the attack (AttackAnimation::showsAttack)
    bool chainSawKill = false;          // Chain Saw's instant kill (AttackAnimation::chainSawKill)
    // A Dance that changes the battlefield: the background it changes to.
    std::optional<BattleBackgroundId> newBackground;

    AttackAnimationParams params;
    AttackScratch scratch;
};

// The battle command aimed at the Roulette target: Lore's Roulette when the battle chooses it.
inline constexpr BattleCommandId kRouletteCommand = static_cast<BattleCommandId>(0x1E);

// The parts of an attack that other parts of the battle work out. Every member does nothing by
// default. They are called in the order an attack is worked out:
//
//   baseDamage        the damage before it is split or multiplied (attack.damage)
//   attackerEffect    the special effect on the attacker
//   scaleDamage       the damage multiplied by attack.multiplier
//   controlMenu       a controlled monster acted: its controller's menu is redrawn
//   statusEffect      the statuses the attack puts on or takes off a target
//   targetEffect      the special effect on a target; true if the effect missed it
//   targetDamage      the damage to one target
//   applyDamage       every target's damage is taken and shown
//   reflectedAttack   a spell bounced off Reflect goes on to its new targets
//   applyPendingStatuses  the statuses set and cleared by the attack take effect
class AttackHooks {
public:
    virtual ~AttackHooks() = default;

    virtual void baseDamage(BattleState&, AttackInFlight&, BattlerSlot /*attacker*/) {}
    virtual void attackerEffect(BattleState&, AttackInFlight&, BattlerSlot /*attacker*/) {}
    virtual void scaleDamage(BattleState&, AttackInFlight&) {}
    virtual void controlMenu(BattleState&, BattlerSlot /*controller*/, BattlerSlot /*monster*/) {}
    virtual void statusEffect(BattleState&, AttackInFlight&, BattlerSlot /*attacker*/, BattlerSlot /*target*/) {}
    virtual bool targetEffect(BattleState&, AttackInFlight&, BattlerSlot /*attacker*/, BattlerSlot /*target*/) {
        return false;
    }
    virtual void targetDamage(BattleState&, AttackInFlight&, BattlerSlot /*attacker*/, BattlerSlot /*target*/) {}
    virtual void applyDamage(BattleState&, AttackInFlight&, BattleScriptBuilder&) {}
    virtual void reflectedAttack(BattleState&, AttackInFlight&, BattlerSlot /*attacker*/, BattleScriptBuilder&) {}
    virtual void applyPendingStatuses(BattleState&) {}
};

// A command's attack as it begins: aimed where the command was aimed, with the MP it costs.
AttackInFlight startAttack(const CommandAction& command);

// Copy the attack's record into the attack in flight and take its special effect.
void loadAttackRecord(AttackInFlight& attack, AttackId id);

// Note the attack's targets as the current ones and add them to the command's earlier targets.
// While the attack is re-aimed at random (AttackScratch::randomizeTargets — a weapon's spell cast
// at random, a sketched attack, Umaro's Storm, Runic drawing a spell in), the current targets are
// taken back instead.
void rememberTargets(AttackInFlight& attack);

// Run every attack of the command: aim each one, check it against each target, work out its
// effects through `hooks`, and write it into `script`.
void runAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
               BattlerSlot attacker);

// runAttack() aimed at the attacker itself, by the attack's own targeting even if the attacker is
// gone or vanished.
void runSelfAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                   BattlerSlot attacker);

// One attack: aim it, cover the target, pay its MP, decide who it hits and whether it is a
// critical hit, and work out its effect on every target hit.
void resolveAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                   BattlerSlot attacker);

// Whether an attack reaches one target, and what the player sees if it does not.
struct HitResult {
    bool hit = false;
    BlockShown block = BlockShown::None;  // a party member's block, or the Golem or Interceptor stepping in
    bool reflected = false;               // it bounced off the target's Reflect
};

// The hit check for one target. A Golem or Interceptor that steps in counts as a hit on the target
// and takes away the attack's statuses.
HitResult checkHit(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target);

// The targets hit in the back: in a side attack, by a party member's blow, the monsters facing
// the same way it does; in a pincer, by a monster's blow, the party members facing its way.
BattlerSet hitInTheBack(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker);

// Record on `target` who hit it and with what, and mark it to answer the attacker.
void recordLastHit(BattleState& state, const AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target);

}  // namespace ostinato::battle
