// A battle script: the sequence of things one action shows.
//
// The battle builds a script for every action it runs and hands it to the presentation as a
// RunBattleScript request (battle/battle_requests.h); the battle waits until it has played. Each
// command is a value saying what the player sees next — a name in the window at the top of the
// screen, an animation, a message — with the battlers and ids it shows.
//
//     BattleScriptBuilder script;
//     script.add(AttackNameShown{.attack = AttackId::FIRE});
//     script.addAnimation(AttackAnimation{.what = BattleCommandId::MAGIC, .attack = AttackId::FIRE});
//     script.addParams(params);          // who cast it, whom it reached, what blocked it
//     std::vector<BattleScriptCommand> commands = script.take();
//
// An animation's AttackAnimationParams are added separately from the animation itself, in the
// order they are worked out; take() gives each animation the params added for it, in order. An
// attack writes its animation before it knows which targets it hit, and adds the params once it
// does.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_commands.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>
#include <vector>

#include "battle/battler_set.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

namespace ostinato::battle {

// A character steps forward for the action it has chosen, while its advance wait runs: the ready
// stance, or the leap into the air for a Jump.
struct ReadyStance {
    BattlerSlot actor = BattlerSlot::character(0);
    BattleCommandId command = BattleCommandId::FIGHT;
    BattlerSet targets;
};

// The battler that just acted goes back to its place.
struct StepBack {};

// In the final battle, the next stage arrives: the background scrolls to `scroll` and the stage's
// monsters appear.
struct FinalBattleStageArrives {
    std::uint8_t scroll = 0;
    BattlerSet monsters;
};

// What an attack shows on a party member who blocked it: the equipment's block animation, or the
// Golem or Interceptor stepping in.
enum class BlockShown : std::uint8_t {
    None = 0,
    Dagger = 1,
    Sword = 2,
    Shield = 3,
    ZephyrCape = 4,
    Golem = 6,
    Interceptor = 7,
};

// Who an animation is about: the attacker, its targets, who it hit and who blocked it.
struct AttackAnimationParams {
    bool attackerIsMonster = false;   // the attacker is in a monster slot
    bool noCharacterTargets = false;  // no party member was aimed at
    bool criticalFlash = false;       // a critical hit: the screen flashes
    bool castingSwirl = false;        // the swirl that comes before a spell plays
    bool dragonHorn = false;          // the last landing of a Dragon Horn jump
    bool monsterAttack = false;       // a monster's special attack, shown as its attack swing
    BattlerSlot attacker = BattlerSlot::character(0);
    BattlerSet targets;               // aimed at
    BattlerSet targetsHit;            // hit, once the hit check has run
    BattlerSet reflectedOff;          // the targets the attack bounced off
    // A party member was covered: the ally stepped in front of, and every battler who stepped in
    // front of it (only one of them takes the hit).
    std::optional<BattlerSlot> coveredAlly;
    BattlerSet steppingIn;
    std::array<BlockShown, kPartySlots> blocks{};  // per party slot
};

// The animations past the commands' own: things the battle shows that no command chose.
enum class SpecialAnimation : std::uint8_t {
    DanceStumble = 0x20,
    RouletteCursor = 0x21,
    PartyRunsAway = 0x22,
    UmaroTackle = 0x23,
    UmaroThrow = 0x24,
    RunicAbsorbsSpell = 0x25,  // the Runic sword drawing in a spell aimed at a party member
    DiceRoll = 0x26,
};

// Mimic's command animation is empty: an attack shown as Mimic shows no animation.
inline constexpr BattleCommandId kNothingShown = BattleCommandId::MIMIC;

// The animation of an attack: the command's own (Fight's swing, a spell's effect for Magic) or a
// special one.
struct AttackAnimation {
    std::variant<BattleCommandId, SpecialAnimation> what = BattleCommandId::FIGHT;
    AttackId attack{};
    bool offHand = false;        // a Fight: the swing is with the shield hand
    // The second thing an animation needs, by command: for a Fight the weapon's animation (the
    // item id plus one; a monster's attack graphic plus one), for a monster's special attack its
    // special animation.
    std::uint8_t secondary = 0;
    // Item, Throw and Tools show the item used, thrown or worked in place of an attack.
    std::optional<ItemId> item;
    // Chain Saw's instant kill: the tool's other animation.
    bool chainSawKill = false;
    // False when the command's own animation plays without the attack's: Bushido readying Retort.
    bool showsAttack = true;
    // A Dance that changed the battlefield: the background it changes to as the dance plays.
    std::optional<BattleBackgroundId> newBackground;
    AttackAnimationParams params;
};

// A name in the window at the top of the screen, before the attack plays.
struct AttackNameShown {
    AttackId attack{};  // the attack-name entry at this attack's index
};

struct BushidoNameShown {
    AttackId technique{};
};

struct ItemNameShown {
    ItemId item = ItemId::EMPTY;
};

struct EsperNameShown {
    EsperId esper{};
};

struct CommandNameShown {
    BattleCommandId command = BattleCommandId::FIGHT;
};

// A monster's special attack, under the monster's own name for it.
struct MonsterSpecialAttackNameShown {
    MonsterId monster{};
};

// The attack-name entry at Dispatch's index reads "Joker Doom": Bushido techniques show their
// names from their own list, so Slot's Joker Doom uses the slot.
inline constexpr AttackId kJokerDoomName = AttackId::DISPATCH;

// A battle message in the wide window. A message that shows numbers carries them.
struct BattleMessage {
    std::uint8_t message = 0;
    std::optional<std::uint32_t> firstNumber;
    std::optional<std::uint32_t> secondNumber;
};

// The battle messages the commands show.
inline constexpr std::uint8_t kMessageStumbled = 6;
inline constexpr std::uint8_t kMessageMuguMugu = 7;
inline constexpr std::uint8_t kMessageNotEnoughMp = 40;
inline constexpr std::uint8_t kMessageBlitzFailed = 67;

// A party member goes back to standing normally.
struct PoseReset {
    BattlerSlot battler = BattlerSlot::character(0);
};

using BattleScriptCommand =
    std::variant<ReadyStance, StepBack, FinalBattleStageArrives, AttackAnimation, AttackNameShown, BushidoNameShown,
                 ItemNameShown, EsperNameShown, CommandNameShown, MonsterSpecialAttackNameShown, BattleMessage,
                 PoseReset>;

// Builds one action's battle script.
class BattleScriptBuilder {
public:
    void add(BattleScriptCommand command);

    // Add an animation and remember where it went: a later replaceAnimation() writes over it.
    void addAnimation(AttackAnimation animation);

    // Write `command` where the last remembered animation went, in place of it. Does nothing if
    // no animation has been added.
    void replaceAnimation(BattleScriptCommand command);

    // The params of the next animation that has none yet.
    void addParams(const AttackAnimationParams& params);

    // The attacker named by the last params added.
    std::optional<BattlerSlot> lastParamsAttacker() const;

    const std::vector<BattleScriptCommand>& commands() const { return commands_; }

    // The finished script: each animation carries the params added for it, in order.
    std::vector<BattleScriptCommand> take();

private:
    std::vector<BattleScriptCommand> commands_;
    std::deque<AttackAnimationParams> params_;
    std::optional<std::size_t> animationSlot_;
};

}  // namespace ostinato::battle
