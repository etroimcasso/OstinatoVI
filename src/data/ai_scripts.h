// The monster AI scripts: what each monster does on its turn and how it answers an attack.
//
// There is one script for every MonsterId, found by the same index; a scripted battle gives a
// party member one of the scripts from 256 up. A script has two parts, each a list of steps
// ending in AiStep::end():
//
//   turn     read on the monster's turn
//   counter  read when the monster is attacked; empty for a monster that answers nothing
//
// A step is an action (use an attack, show a dialog, change a variable), a condition, a wait, or
// the end of a block. Conditions and the steps after them up to AiStep::endIf() form a block: the
// block runs only if every one of its conditions holds.
//
//     const AiScript& script = aiScript(MonsterId::LOBO);
//     for (const AiStep& step : script.turn) {
//         if (const auto* attack = step.as<ai::Attack>()) {
//             use(attack->attack);
//         }
//     }
//
// The steps are generated from the game's own script source, one named array per part
// (src/data/generated/ai_script_steps_data.inc), and the table that points at them is
// src/data/generated/ai_scripts_data.inc. A script row reads as the builders below are named:
//
//     AiStep::ifHpAtMost({ .target = AiTargetSelector::SELF, .hp = 10240 }),
//     AiStep::setTarget(AiTargetSelector::ALL_CHARS),
//     AiStep::attackOneOf({ AttackId::FIRE_2, AttackId::FIRE_2, kNoAttack }),
//     AiStep::endIf(),
//
// docs/engine/data-layer/monster-ai-scripts.md describes every step and how to change a script.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>

#include "ostinato/ai_target_selector.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/battle_slot_mask.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/element_set.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_animation.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_id.h"
#include "ostinato/monster_visibility_mode.h"
#include "ostinato/status_id.h"

namespace ostinato {

// How many scripts there are: one per MonsterId.
inline constexpr std::size_t kAiScriptCount = 384;

// How many variables a battle keeps for its scripts to share. They are 0 as the battle starts.
inline constexpr std::size_t kAiBattleVariables = 4;

// One of three choices that is no choice: a step that draws it does nothing.
inline constexpr std::nullopt_t kNoAttack{std::nullopt};
inline constexpr std::nullopt_t kNoCommand{std::nullopt};
inline constexpr std::nullopt_t kNoItem{std::nullopt};

// Who a step names: one party member by who it is, or a selector — a slot, a side, a random
// member of a side, the battler that last attacked. A step takes either:
//
//     AiStep::setTarget(AiTargetSelector::RAND_CHAR)
//     AiStep::setTarget(CharacterPropId::BANON)
class AiTarget {
public:
    constexpr AiTarget() = default;
    constexpr AiTarget(AiTargetSelector selector) : selector_(selector) {}
    constexpr AiTarget(CharacterPropId character) : character_(character), isCharacter_(true) {}

    constexpr bool isCharacter() const { return isCharacter_; }

    // The party member named. Only for a target that is a character.
    constexpr CharacterPropId character() const { return character_; }

    // The selector named. Only for a target that is not a character.
    constexpr AiTargetSelector selector() const { return selector_; }

    friend constexpr bool operator==(const AiTarget&, const AiTarget&) = default;

private:
    CharacterPropId character_{};
    AiTargetSelector selector_ = AiTargetSelector::NOTHING;
    bool isCharacter_ = false;
};

// The monster slots a step names. self() is the monster the script belongs to; all() is every
// slot, of which the battle keeps the ones that hold a monster.
//
//     AiMonsters::of(AiTargetSelector::MONSTER_SLOT_2, AiTargetSelector::MONSTER_SLOT_4)
struct AiMonsters {
    BattleSlotMask slots;

    static constexpr AiMonsters self() { return AiMonsters{}; }

    static constexpr AiMonsters all() { return AiMonsters{BattleSlotMask{0xFF}}; }

    // The slots named by MONSTER_SLOT_1 to MONSTER_SLOT_6. Any other selector does not compile.
    static constexpr AiMonsters of(std::same_as<AiTargetSelector> auto... slots) {
        AiMonsters monsters;
        (monsters.add(slots), ...);
        return monsters;
    }

    constexpr bool isSelf() const { return slots.bits == 0; }

    friend constexpr bool operator==(const AiMonsters& a, const AiMonsters& b) {
        return a.slots.bits == b.slots.bits;
    }

private:
    constexpr void add(AiTargetSelector slot) {
        const int position =
            static_cast<int>(slot) - static_cast<int>(AiTargetSelector::MONSTER_SLOT_1);
        if (position < 0 || position > 5) {
            throw std::invalid_argument("AiMonsters::of() takes MONSTER_SLOT_1 to MONSTER_SLOT_6");
        }
        slots.bits = static_cast<std::uint8_t>(slots.bits | (1u << position));
    }
};

// A variable a script reads and changes: one of the battle's kAiBattleVariables, which every
// script in the battle shares, or the monster's own.
class AiVariable {
public:
    constexpr AiVariable() = default;

    static constexpr AiVariable battle(std::uint8_t index) {
        if (index >= kAiBattleVariables) {
            throw std::out_of_range("a battle has four variables, 0 to 3");
        }
        return AiVariable{index, false};
    }

    static constexpr AiVariable own() { return AiVariable{0, true}; }

    constexpr bool isOwn() const { return own_; }

    // Which of the battle's variables. Only for a variable that is not the monster's own.
    constexpr std::uint8_t index() const { return index_; }

    friend constexpr bool operator==(const AiVariable&, const AiVariable&) = default;

private:
    constexpr AiVariable(std::uint8_t index, bool own) : index_(index), own_(own) {}

    std::uint8_t index_ = 0;
    bool own_ = false;
};

// A switch a script tests and changes: one bit (0 to 7) of a variable, or one of the game's
// battle switches by its number (game/battle_switches.h), which outlast the battle.
//
//     AiSwitch::own(0)                 // bit 0 of the monster's own variable
//     AiSwitch::battleVariable(3, 1)   // bit 1 of battle variable 3
//     AiSwitch::battleSwitch(73)       // the game's battle switch 73
class AiSwitch {
public:
    constexpr AiSwitch() = default;

    static constexpr AiSwitch own(std::uint8_t bit) { return AiSwitch{AiVariable::own(), checked(bit), 0, false}; }

    static constexpr AiSwitch battleVariable(std::uint8_t index, std::uint8_t bit) {
        return AiSwitch{AiVariable::battle(index), checked(bit), 0, false};
    }

    static constexpr AiSwitch battleSwitch(int number) {
        if (number < 0 || number >= kBattleSwitches) {
            throw std::out_of_range("the game has battle switches 0 to 159");
        }
        return AiSwitch{AiVariable{}, 0, static_cast<std::uint8_t>(number), true};
    }

    constexpr bool isBattleSwitch() const { return isBattleSwitch_; }

    // The battle switch's number. Only for a battle switch.
    constexpr int number() const { return number_; }

    // The variable and the bit of it. Only for a switch that is not a battle switch.
    constexpr AiVariable variable() const { return variable_; }
    constexpr std::uint8_t bit() const { return bit_; }

    friend constexpr bool operator==(const AiSwitch&, const AiSwitch&) = default;

private:
    static constexpr int kBattleSwitches = 160;

    constexpr AiSwitch(AiVariable variable, std::uint8_t bit, std::uint8_t number, bool isBattleSwitch)
        : variable_(variable), bit_(bit), number_(number), isBattleSwitch_(isBattleSwitch) {}

    static constexpr std::uint8_t checked(std::uint8_t bit) {
        if (bit > 7) {
            throw std::out_of_range("a variable has bits 0 to 7");
        }
        return bit;
    }

    AiVariable variable_;
    std::uint8_t bit_ = 0;
    std::uint8_t number_ = 0;
    bool isBattleSwitch_ = false;
};

// What a step does to a variable, and to a switch.
enum class AiVariableChange : std::uint8_t { Set, Add, Subtract };
enum class AiSwitchChange : std::uint8_t { Toggle, Set, Clear };

// The effects a script can ask for besides attacks, in the order the game numbers them.
enum class AiEffect : std::uint8_t {
    ResetOwnTimer = 0,           // the monster's own timer starts again from 0
    MakeInvincible = 1,          // damage to the target is not taken
    EndBattle = 2,               // the battle ends at once
    RecruitGau = 3,              // Gau joins the party
    ResetBattleTimer = 4,        // the battle's timer starts again from 0
    EndInvincible = 5,           // damage to the target is taken again
    MakeTargetable = 6,          // the target can be aimed at again
    MakeUntargetable = 7,        // the target cannot be aimed at
    FillGauge = 8,               // the monster's gauge fills, if the target is there
    EndBattleLearningRages = 9,  // the battle ends and Gau learns the rages
    Nothing = 10,                // no effect; no script holds it
    SetStatus = 11,              // the monster gives itself a status
    ClearStatus = 12,            // the monster takes a status off itself
    HideSelf = 13,               // the monster hides
};

// What each kind of step holds. A script row is written through AiStep's builders; code that
// reads a script asks a step for one of these (AiStep::as<>()).
namespace ai {

// --- actions ---------------------------------------------------------------------------------

// Use one attack. Nothing is drawn.
struct Attack {
    AttackId attack{};
    friend constexpr bool operator==(const Attack&, const Attack&) = default;
};

// Use one of three attacks, each as likely as the others. A choice of kNoAttack does nothing.
struct AttackOneOf {
    std::array<std::optional<AttackId>, 3> choices{};
    friend constexpr bool operator==(const AttackOneOf&, const AttackOneOf&) = default;
};

// Aim the actions that follow at `target`.
struct SetTarget {
    AiTarget target;
    friend constexpr bool operator==(const SetTarget&, const SetTarget&) = default;
};

// The battle moves to another formation: the monsters on the field leave by `exit`. With
// `restoreMonsters` each new monster starts at full HP and MP; `scrollBackground` scrolls the
// background on first.
struct ChangeBattle {
    FormationId formation{};
    MonsterEntranceType exit = MonsterEntranceType::PRE_DRAWN;
    bool restoreMonsters = false;
    bool scrollBackground = false;
    friend constexpr bool operator==(const ChangeBattle&, const ChangeBattle&) = default;
};

// The monster says something: one of the monster dialogs, by its index.
struct Dialog {
    std::uint16_t dialog = 0;
    friend constexpr bool operator==(const Dialog&, const Dialog&) = default;
};

// Use one of three battle commands, each as likely as the others. kNoCommand does nothing.
struct CommandOneOf {
    std::array<std::optional<BattleCommandId>, 3> choices{};
    friend constexpr bool operator==(const CommandOneOf&, const CommandOneOf&) = default;
};

// Monsters come onto the field or leave it, shown by `type`.
struct ShowOrHideMonsters {
    MonsterVisibilityMode mode = MonsterVisibilityMode::ReviveAtFullHp;
    AiMonsters monsters;
    MonsterEntranceType type = MonsterEntranceType::PRE_DRAWN;
    friend constexpr bool operator==(const ShowOrHideMonsters&, const ShowOrHideMonsters&) = default;
};

// Use an item, or throw it: `twoInThree` two times in three, `oneInThree` the third. kNoItem
// does nothing.
struct ItemOneOf {
    bool thrown = false;
    std::optional<ItemId> twoInThree;
    std::optional<ItemId> oneInThree;
    friend constexpr bool operator==(const ItemOneOf&, const ItemOneOf&) = default;
};

// A battle event plays: a scene scripted into the battle, by its index.
struct BattleEvent {
    std::uint8_t event = 0;
    friend constexpr bool operator==(const BattleEvent&, const BattleEvent&) = default;
};

// A variable is set to `amount`, or has it added or subtracted. `amount` is at most 63.
struct ChangeVariable {
    AiVariable variable;
    AiVariableChange change = AiVariableChange::Set;
    std::uint8_t amount = 0;
    friend constexpr bool operator==(const ChangeVariable&, const ChangeVariable&) = default;
};

struct ChangeSwitch {
    AiSwitch which;
    AiSwitchChange change = AiSwitchChange::Set;
    friend constexpr bool operator==(const ChangeSwitch&, const ChangeSwitch&) = default;
};

// An animation plays on `monsters`. PartyRunsLeftToRight and PartyRunsRightToLeft name no
// monsters; SoundEffect plays sound effect `sound` panned by `pan` instead.
struct Animate {
    MonsterAnimation animation = MonsterAnimation::FlashRed;
    AiMonsters monsters;
    std::uint8_t sound = 0;
    std::uint8_t pan = 0;
    friend constexpr bool operator==(const Animate&, const Animate&) = default;
};

// One of the AiEffect effects. `target` is read by the four that name one and by FillGauge;
// `status` by SetStatus and ClearStatus.
struct Effect {
    AiEffect effect = AiEffect::Nothing;
    AiTarget target;
    StatusId status{};
    friend constexpr bool operator==(const Effect&, const Effect&) = default;
};

// The turn ends here; the next turn goes on from the step after this one.
struct Wait {
    friend constexpr bool operator==(const Wait&, const Wait&) = default;
};

// The end of a block. Reaching it ends the turn, and the next turn starts from the top.
struct EndIf {
    friend constexpr bool operator==(const EndIf&, const EndIf&) = default;
};

// The end of a part.
struct End {
    friend constexpr bool operator==(const End&, const End&) = default;
};

// --- conditions, in the order the game numbers them -------------------------------------------

struct Never {
    friend constexpr bool operator==(const Never&, const Never&) = default;
};

// The monster was last hit with a battle command, and it is `first` or `second`.
struct HitByCommand {
    BattleCommandId first{};
    BattleCommandId second{};
    friend constexpr bool operator==(const HitByCommand&, const HitByCommand&) = default;
};

// The same for the last spell or skill it was hit with.
struct HitByAttack {
    AttackId first{};
    AttackId second{};
    friend constexpr bool operator==(const HitByAttack&, const HitByAttack&) = default;
};

// The same for the last item used on it.
struct HitByItem {
    ItemId first{};
    ItemId second{};
    friend constexpr bool operator==(const HitByItem&, const HitByItem&) = default;
};

// The attack it was last hit with has one of `elements`.
struct HitByElement {
    ElementSet elements;
    friend constexpr bool operator==(const HitByElement& a, const HitByElement& b) {
        return a.elements.bits == b.elements.bits;
    }
};

// Some attack has taken HP or MP from it.
struct Damaged {
    friend constexpr bool operator==(const Damaged&, const Damaged&) = default;
};

// The target's HP is `hp` or less. `hp` is a multiple of 128.
struct HpAtMost {
    AiTarget target;
    std::uint16_t hp = 0;
    friend constexpr bool operator==(const HpAtMost&, const HpAtMost&) = default;
};

struct MpAtMost {
    AiTarget target;
    std::uint8_t mp = 0;
    friend constexpr bool operator==(const MpAtMost&, const MpAtMost&) = default;
};

// One of the battlers the target names has the status.
struct HasStatus {
    AiTarget target;
    StatusId status{};
    friend constexpr bool operator==(const HasStatus&, const HasStatus&) = default;
};

// None of the battlers the target names has the status.
struct NoStatus {
    AiTarget target;
    StatusId status{};
    friend constexpr bool operator==(const NoStatus&, const NoStatus&) = default;
};

// Half the monster's own timer is `value` or more.
struct HalfOwnTimerAtLeast {
    std::uint8_t value = 0;
    friend constexpr bool operator==(const HalfOwnTimerAtLeast&, const HalfOwnTimerAtLeast&) = default;
};

struct VariableBelow {
    AiVariable variable;
    std::uint8_t value = 0;
    friend constexpr bool operator==(const VariableBelow&, const VariableBelow&) = default;
};

struct VariableAtLeast {
    AiVariable variable;
    std::uint8_t value = 0;
    friend constexpr bool operator==(const VariableAtLeast&, const VariableAtLeast&) = default;
};

struct LevelBelow {
    AiTarget target;
    std::uint8_t level = 0;
    friend constexpr bool operator==(const LevelBelow&, const LevelBelow&) = default;
};

struct LevelAtLeast {
    AiTarget target;
    std::uint8_t level = 0;
    friend constexpr bool operator==(const LevelAtLeast&, const LevelAtLeast&) = default;
};

// The list of monster names on screen holds one name or none.
struct OneKindOfMonster {
    friend constexpr bool operator==(const OneKindOfMonster&, const OneKindOfMonster&) = default;
};

// Every one of `monsters` is alive.
struct MonstersAlive {
    AiMonsters monsters;
    friend constexpr bool operator==(const MonstersAlive&, const MonstersAlive&) = default;
};

// Every one of `monsters` is a monster of the formation that is no longer alive.
struct MonstersDead {
    AiMonsters monsters;
    friend constexpr bool operator==(const MonstersDead&, const MonstersDead&) = default;
};

// At least `count` party members are still fighting.
struct PartyAtLeast {
    std::uint8_t count = 0;
    friend constexpr bool operator==(const PartyAtLeast&, const PartyAtLeast&) = default;
};

// At most `count` monsters are still fighting.
struct MonstersAtMost {
    std::uint8_t count = 0;
    friend constexpr bool operator==(const MonstersAtMost&, const MonstersAtMost&) = default;
};

struct SwitchSet {
    AiSwitch which;
    friend constexpr bool operator==(const SwitchSet&, const SwitchSet&) = default;
};

struct SwitchClear {
    AiSwitch which;
    friend constexpr bool operator==(const SwitchClear&, const SwitchClear&) = default;
};

// Half the battle's timer is `value` or more.
struct HalfBattleTimerAtLeast {
    std::uint8_t value = 0;
    friend constexpr bool operator==(const HalfBattleTimerAtLeast&, const HalfBattleTimerAtLeast&) = default;
};

// The target names at least one battler.
struct TargetValid {
    AiTarget target;
    friend constexpr bool operator==(const TargetValid&, const TargetValid&) = default;
};

struct GauNotRecruited {
    friend constexpr bool operator==(const GauNotRecruited&, const GauNotRecruited&) = default;
};

// The monster stands in one of `slots`.
struct InSlot {
    AiMonsters slots;
    friend constexpr bool operator==(const InSlot&, const InSlot&) = default;
};

// The target is weak to one of `elements`.
struct WeakTo {
    AiTarget target;
    ElementSet elements;
    friend constexpr bool operator==(const WeakTo& a, const WeakTo& b) {
        return a.target == b.target && a.elements.bits == b.elements.bits;
    }
};

// The formation being fought is `formation`.
struct FormationIs {
    FormationId formation{};
    friend constexpr bool operator==(const FormationIs&, const FormationIs&) = default;
};

struct Always {
    friend constexpr bool operator==(const Always&, const Always&) = default;
};

// Three choices, each as likely as the others, as attackOneOf() and commandOneOf() take them.
template <typename T>
struct OneOfThree {
    std::optional<T> first;
    std::optional<T> second;
    std::optional<T> third;
};

// A variable and a number, as the builders that take both are written.
struct VariableValue {
    AiVariable variable;
    std::uint8_t value = 0;
};

struct VariableAmount {
    AiVariable variable;
    std::uint8_t amount = 0;
};

// A sound effect and where it is panned.
struct Sound {
    std::uint8_t sound = 0;
    std::uint8_t pan = 0;
};

// An animation and the monsters it plays on.
struct MonsterAnimationOn {
    MonsterAnimation animation = MonsterAnimation::FlashRed;
    AiMonsters monsters;
};

// The two items of a use or a throw.
struct ItemChoices {
    std::optional<ItemId> twoInThree;
    std::optional<ItemId> oneInThree;
};

}  // namespace ai

// A condition: one of the tests a block opens with.
using AiCondition =
    std::variant<ai::Never, ai::HitByCommand, ai::HitByAttack, ai::HitByItem, ai::HitByElement, ai::Damaged,
                 ai::HpAtMost, ai::MpAtMost, ai::HasStatus, ai::NoStatus, ai::HalfOwnTimerAtLeast,
                 ai::VariableBelow, ai::VariableAtLeast, ai::LevelBelow, ai::LevelAtLeast, ai::OneKindOfMonster,
                 ai::MonstersAlive, ai::MonstersDead, ai::PartyAtLeast, ai::MonstersAtMost, ai::SwitchSet,
                 ai::SwitchClear, ai::HalfBattleTimerAtLeast, ai::TargetValid, ai::GauNotRecruited, ai::InSlot,
                 ai::WeakTo, ai::FormationIs, ai::Always>;

// One step of a script.
struct AiStep {
    using Kind = std::variant<ai::Attack, ai::AttackOneOf, ai::SetTarget, ai::ChangeBattle, ai::Dialog,
                              ai::CommandOneOf, ai::ShowOrHideMonsters, ai::ItemOneOf, ai::BattleEvent,
                              ai::ChangeVariable, ai::ChangeSwitch, ai::Animate, ai::Effect, AiCondition,
                              ai::Wait, ai::EndIf, ai::End>;

    Kind kind;

    // The step as a `T`, or null when it is another kind of step. `T` is one of the ai:: action
    // types or AiCondition.
    template <typename T>
    constexpr const T* as() const {
        return std::get_if<T>(&kind);
    }

    // The condition as a `T`, or null when the step is no condition or another one.
    template <typename T>
    constexpr const T* condition() const {
        const AiCondition* held = std::get_if<AiCondition>(&kind);
        return held == nullptr ? nullptr : std::get_if<T>(held);
    }

    constexpr bool isCondition() const { return std::holds_alternative<AiCondition>(kind); }

    friend constexpr bool operator==(const AiStep&, const AiStep&) = default;

    // --- actions ------------------------------------------------------------------------------

    static constexpr AiStep attack(AttackId attack) { return {ai::Attack{attack}}; }

    static constexpr AiStep attackOneOf(ai::OneOfThree<AttackId> choices) {
        return {ai::AttackOneOf{{{choices.first, choices.second, choices.third}}}};
    }

    static constexpr AiStep setTarget(AiTarget target) { return {ai::SetTarget{target}}; }

    static constexpr AiStep changeBattle(ai::ChangeBattle change) { return {change}; }

    static constexpr AiStep dialog(std::uint16_t dialog) { return {ai::Dialog{dialog}}; }

    static constexpr AiStep commandOneOf(ai::OneOfThree<BattleCommandId> choices) {
        return {ai::CommandOneOf{{{choices.first, choices.second, choices.third}}}};
    }

    static constexpr AiStep showOrHideMonsters(ai::ShowOrHideMonsters change) { return {change}; }

    static constexpr AiStep useItemOneOf(ai::ItemChoices items) {
        return {ai::ItemOneOf{.thrown = false, .twoInThree = items.twoInThree, .oneInThree = items.oneInThree}};
    }

    static constexpr AiStep throwItemOneOf(ai::ItemChoices items) {
        return {ai::ItemOneOf{.thrown = true, .twoInThree = items.twoInThree, .oneInThree = items.oneInThree}};
    }

    static constexpr AiStep battleEvent(std::uint8_t event) { return {ai::BattleEvent{event}}; }

    static constexpr AiStep setVariable(ai::VariableValue to) {
        return {ai::ChangeVariable{to.variable, AiVariableChange::Set, checkedAmount(to.value)}};
    }

    static constexpr AiStep addToVariable(ai::VariableAmount by) {
        return {ai::ChangeVariable{by.variable, AiVariableChange::Add, checkedAmount(by.amount)}};
    }

    static constexpr AiStep subtractFromVariable(ai::VariableAmount by) {
        return {ai::ChangeVariable{by.variable, AiVariableChange::Subtract, checkedAmount(by.amount)}};
    }

    static constexpr AiStep toggleSwitch(AiSwitch which) { return {ai::ChangeSwitch{which, AiSwitchChange::Toggle}}; }
    static constexpr AiStep setSwitch(AiSwitch which) { return {ai::ChangeSwitch{which, AiSwitchChange::Set}}; }
    static constexpr AiStep clearSwitch(AiSwitch which) { return {ai::ChangeSwitch{which, AiSwitchChange::Clear}}; }

    static constexpr AiStep animate(ai::MonsterAnimationOn animation) {
        return {ai::Animate{.animation = animation.animation, .monsters = animation.monsters}};
    }

    static constexpr AiStep partyRunsLeftToRight() {
        return {ai::Animate{.animation = MonsterAnimation::PartyRunsLeftToRight}};
    }

    static constexpr AiStep partyRunsRightToLeft() {
        return {ai::Animate{.animation = MonsterAnimation::PartyRunsRightToLeft}};
    }

    static constexpr AiStep soundEffect(ai::Sound sound) {
        return {ai::Animate{.animation = MonsterAnimation::SoundEffect, .sound = sound.sound, .pan = sound.pan}};
    }

    static constexpr AiStep resetOwnTimer() { return effect(AiEffect::ResetOwnTimer); }
    static constexpr AiStep makeInvincible(AiTarget target) { return effect(AiEffect::MakeInvincible, target); }
    static constexpr AiStep endBattle() { return effect(AiEffect::EndBattle); }
    static constexpr AiStep recruitGau() { return effect(AiEffect::RecruitGau); }
    static constexpr AiStep resetBattleTimer() { return effect(AiEffect::ResetBattleTimer); }
    static constexpr AiStep endInvincible(AiTarget target) { return effect(AiEffect::EndInvincible, target); }
    static constexpr AiStep makeTargetable(AiTarget target) { return effect(AiEffect::MakeTargetable, target); }
    static constexpr AiStep makeUntargetable(AiTarget target) { return effect(AiEffect::MakeUntargetable, target); }
    static constexpr AiStep fillGauge(AiTarget target) { return effect(AiEffect::FillGauge, target); }
    static constexpr AiStep endBattleLearningRages() { return effect(AiEffect::EndBattleLearningRages); }

    static constexpr AiStep setStatus(StatusId status) {
        return {ai::Effect{.effect = AiEffect::SetStatus, .status = status}};
    }

    static constexpr AiStep clearStatus(StatusId status) {
        return {ai::Effect{.effect = AiEffect::ClearStatus, .status = status}};
    }

    static constexpr AiStep hideSelf() { return effect(AiEffect::HideSelf); }

    static constexpr AiStep wait() { return {ai::Wait{}}; }
    static constexpr AiStep endIf() { return {ai::EndIf{}}; }
    static constexpr AiStep end() { return {ai::End{}}; }

    // --- conditions ---------------------------------------------------------------------------

    static constexpr AiStep never() { return test(ai::Never{}); }

    static constexpr AiStep ifHitByCommand(BattleCommandId command) { return test(ai::HitByCommand{command, command}); }
    static constexpr AiStep ifHitByCommand(BattleCommandId first, BattleCommandId second) {
        return test(ai::HitByCommand{first, second});
    }

    static constexpr AiStep ifHitByAttack(AttackId attack) { return test(ai::HitByAttack{attack, attack}); }
    static constexpr AiStep ifHitByAttack(AttackId first, AttackId second) {
        return test(ai::HitByAttack{first, second});
    }

    static constexpr AiStep ifHitByItem(ItemId item) { return test(ai::HitByItem{item, item}); }
    static constexpr AiStep ifHitByItem(ItemId first, ItemId second) { return test(ai::HitByItem{first, second}); }

    static constexpr AiStep ifHitByElement(ElementSet elements) { return test(ai::HitByElement{elements}); }
    static constexpr AiStep ifDamaged() { return test(ai::Damaged{}); }

    static constexpr AiStep ifHpAtMost(ai::HpAtMost condition) {
        if (condition.hp % kHpStep != 0) {
            throw std::invalid_argument("an HP condition is a multiple of 128");
        }
        return test(condition);
    }

    static constexpr AiStep ifMpAtMost(ai::MpAtMost condition) { return test(condition); }
    static constexpr AiStep ifStatus(ai::HasStatus condition) { return test(condition); }
    static constexpr AiStep ifNoStatus(ai::NoStatus condition) { return test(condition); }
    static constexpr AiStep ifHalfOwnTimerAtLeast(std::uint8_t value) { return test(ai::HalfOwnTimerAtLeast{value}); }
    static constexpr AiStep ifVariableBelow(ai::VariableBelow condition) { return test(condition); }
    static constexpr AiStep ifVariableAtLeast(ai::VariableAtLeast condition) { return test(condition); }
    static constexpr AiStep ifLevelBelow(ai::LevelBelow condition) { return test(condition); }
    static constexpr AiStep ifLevelAtLeast(ai::LevelAtLeast condition) { return test(condition); }
    static constexpr AiStep ifOneKindOfMonster() { return test(ai::OneKindOfMonster{}); }
    static constexpr AiStep ifMonstersAlive(AiMonsters monsters) { return test(ai::MonstersAlive{monsters}); }
    static constexpr AiStep ifMonstersDead(AiMonsters monsters) { return test(ai::MonstersDead{monsters}); }
    static constexpr AiStep ifPartyAtLeast(std::uint8_t count) { return test(ai::PartyAtLeast{count}); }
    static constexpr AiStep ifMonstersAtMost(std::uint8_t count) { return test(ai::MonstersAtMost{count}); }
    static constexpr AiStep ifSwitchSet(AiSwitch which) { return test(ai::SwitchSet{which}); }
    static constexpr AiStep ifSwitchClear(AiSwitch which) { return test(ai::SwitchClear{which}); }

    static constexpr AiStep ifHalfBattleTimerAtLeast(std::uint8_t value) {
        return test(ai::HalfBattleTimerAtLeast{value});
    }

    static constexpr AiStep ifTargetValid(AiTarget target) { return test(ai::TargetValid{target}); }
    static constexpr AiStep ifGauNotRecruited() { return test(ai::GauNotRecruited{}); }

    static constexpr AiStep ifInSlot(AiMonsters slots) {
        if (slots.isSelf()) {
            throw std::invalid_argument("a slot condition names at least one slot");
        }
        return test(ai::InSlot{slots});
    }

    static constexpr AiStep ifWeakTo(ai::WeakTo condition) { return test(condition); }
    static constexpr AiStep ifFormation(FormationId formation) { return test(ai::FormationIs{formation}); }
    static constexpr AiStep always() { return test(ai::Always{}); }

private:
    // An HP condition holds its HP in steps of this many.
    static constexpr int kHpStep = 128;

    // The most a variable step can set, add or subtract.
    static constexpr std::uint8_t kLargestAmount = 63;

    static constexpr AiStep effect(AiEffect effect, AiTarget target = {}) {
        return {ai::Effect{.effect = effect, .target = target}};
    }

    static constexpr AiStep test(AiCondition condition) { return {condition}; }

    static constexpr std::uint8_t checkedAmount(std::uint8_t amount) {
        if (amount > kLargestAmount) {
            throw std::out_of_range("a variable step's amount is at most 63");
        }
        return amount;
    }
};

// One monster's script.
struct AiScript {
    MonsterId monster{};
    std::span<const AiStep> turn;
    std::span<const AiStep> counter;  // empty: the monster answers no attack

    constexpr bool hasCounter() const { return !counter.empty(); }
};

// The script at `index`: a monster's by its MonsterId, or the one a scripted battle names for a
// party member.
const AiScript& aiScript(std::uint16_t index);
const AiScript& aiScript(MonsterId monster);

// Every script, in MonsterId order.
std::span<const AiScript> aiScripts();

}  // namespace ostinato
