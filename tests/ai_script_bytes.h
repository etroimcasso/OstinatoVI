// A monster AI script's steps as the bytes the cartridge holds them in.
//
// Test code: the port reads scripts as typed steps and never as bytes. Turning a step back into
// its bytes is how a test compares the generated scripts with the cartridge's own, and how a
// place in a script's steps is turned into the place the cartridge's script reader would be at.
//
//     const std::vector<std::uint8_t> bytes = test::aiPartBytes(aiScript(MonsterId::LOBO).turn);
//     const std::size_t offset = test::aiStepOffset(script.turn, 3);   // bytes before step 3
//
// A step is one byte for a single attack; every other step opens with a byte from 0xF0 up and
// runs 1 to 4 bytes (battle/battle_main.asm:4999-5000 gives the sizes).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

#include "data/ai_scripts.h"

namespace ostinato::test {

// The byte three-way choices hold for "no choice".
inline constexpr std::uint8_t kAiNothing = 0xFE;

// A variable byte at or past this is the monster's own; the scripts write this one.
inline constexpr std::uint8_t kAiOwnVariable = 36;

// The variable byte the game's battle switch 0 lives in.
inline constexpr std::uint8_t kAiFirstSwitchVariable = 4;

namespace ai_bytes {

template <typename T>
constexpr std::uint8_t byteOf(T value) {
    return static_cast<std::uint8_t>(value);
}

template <typename T>
constexpr std::uint8_t choice(const std::optional<T>& value) {
    return value ? byteOf(*value) : kAiNothing;
}

constexpr std::uint8_t target(AiTarget target) {
    return target.isCharacter() ? byteOf(target.character()) : byteOf(target.selector());
}

constexpr std::uint8_t variable(AiVariable variable) {
    return variable.isOwn() ? kAiOwnVariable : variable.index();
}

struct SwitchBytes {
    std::uint8_t variable;
    std::uint8_t bit;
};

constexpr SwitchBytes switchBytes(AiSwitch which) {
    if (which.isBattleSwitch()) {
        return {static_cast<std::uint8_t>(kAiFirstSwitchVariable + which.number() / 8),
                static_cast<std::uint8_t>(which.number() % 8)};
    }
    return {variable(which.variable()), which.bit()};
}

// A condition's number and its two operand bytes.
struct ConditionBytes {
    std::uint8_t number;
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};

inline ConditionBytes condition(const AiCondition& condition) {
    return std::visit(
        [](const auto& c) -> ConditionBytes {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ai::Never>) {
                return {0x00};
            } else if constexpr (std::is_same_v<T, ai::HitByCommand>) {
                return {0x01, byteOf(c.first), byteOf(c.second)};
            } else if constexpr (std::is_same_v<T, ai::HitByAttack>) {
                return {0x02, byteOf(c.first), byteOf(c.second)};
            } else if constexpr (std::is_same_v<T, ai::HitByItem>) {
                return {0x03, byteOf(c.first), byteOf(c.second)};
            } else if constexpr (std::is_same_v<T, ai::HitByElement>) {
                return {0x04, c.elements.bits};
            } else if constexpr (std::is_same_v<T, ai::Damaged>) {
                return {0x05};
            } else if constexpr (std::is_same_v<T, ai::HpAtMost>) {
                return {0x06, target(c.target), static_cast<std::uint8_t>(c.hp / 128)};
            } else if constexpr (std::is_same_v<T, ai::MpAtMost>) {
                return {0x07, target(c.target), c.mp};
            } else if constexpr (std::is_same_v<T, ai::HasStatus>) {
                return {0x08, target(c.target), byteOf(c.status)};
            } else if constexpr (std::is_same_v<T, ai::NoStatus>) {
                return {0x09, target(c.target), byteOf(c.status)};
            } else if constexpr (std::is_same_v<T, ai::HalfOwnTimerAtLeast>) {
                return {0x0B, c.value};
            } else if constexpr (std::is_same_v<T, ai::VariableBelow>) {
                return {0x0C, variable(c.variable), c.value};
            } else if constexpr (std::is_same_v<T, ai::VariableAtLeast>) {
                return {0x0D, variable(c.variable), c.value};
            } else if constexpr (std::is_same_v<T, ai::LevelBelow>) {
                return {0x0E, target(c.target), c.level};
            } else if constexpr (std::is_same_v<T, ai::LevelAtLeast>) {
                return {0x0F, target(c.target), c.level};
            } else if constexpr (std::is_same_v<T, ai::OneKindOfMonster>) {
                return {0x10};
            } else if constexpr (std::is_same_v<T, ai::MonstersAlive>) {
                return {0x11, c.monsters.slots.bits};
            } else if constexpr (std::is_same_v<T, ai::MonstersDead>) {
                return {0x12, c.monsters.slots.bits};
            } else if constexpr (std::is_same_v<T, ai::PartyAtLeast>) {
                return {0x13, 0, c.count};
            } else if constexpr (std::is_same_v<T, ai::MonstersAtMost>) {
                return {0x13, 1, c.count};
            } else if constexpr (std::is_same_v<T, ai::SwitchSet>) {
                const SwitchBytes bytes = switchBytes(c.which);
                return {0x14, bytes.variable, bytes.bit};
            } else if constexpr (std::is_same_v<T, ai::SwitchClear>) {
                const SwitchBytes bytes = switchBytes(c.which);
                return {0x15, bytes.variable, bytes.bit};
            } else if constexpr (std::is_same_v<T, ai::HalfBattleTimerAtLeast>) {
                return {0x16, c.value};
            } else if constexpr (std::is_same_v<T, ai::TargetValid>) {
                return {0x17, target(c.target)};
            } else if constexpr (std::is_same_v<T, ai::GauNotRecruited>) {
                return {0x18};
            } else if constexpr (std::is_same_v<T, ai::InSlot>) {
                return {0x19, c.slots.slots.bits};
            } else if constexpr (std::is_same_v<T, ai::WeakTo>) {
                return {0x1A, target(c.target), c.elements.bits};
            } else if constexpr (std::is_same_v<T, ai::FormationIs>) {
                const auto formation = static_cast<std::uint16_t>(c.formation);
                return {0x1B, static_cast<std::uint8_t>(formation & 0xFF),
                        static_cast<std::uint8_t>(formation >> 8)};
            } else {
                static_assert(std::is_same_v<T, ai::Always>, "every condition has its bytes");
                return {0x1C};
            }
        },
        condition);
}

}  // namespace ai_bytes

// One step's bytes.
inline std::vector<std::uint8_t> aiStepBytes(const AiStep& step) {
    using namespace ai_bytes;
    return std::visit(
        [](const auto& s) -> std::vector<std::uint8_t> {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, ai::Attack>) {
                if (byteOf(s.attack) >= 0xF0) {
                    throw std::logic_error("a single attack step holds an attack below 0xF0");
                }
                return {byteOf(s.attack)};
            } else if constexpr (std::is_same_v<T, ai::AttackOneOf>) {
                return {0xF0, choice(s.choices[0]), choice(s.choices[1]), choice(s.choices[2])};
            } else if constexpr (std::is_same_v<T, ai::SetTarget>) {
                return {0xF1, target(s.target)};
            } else if constexpr (std::is_same_v<T, ai::ChangeBattle>) {
                const auto formation = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(s.formation) | (s.restoreMonsters ? 0x8000u : 0u));
                return {0xF2, static_cast<std::uint8_t>(byteOf(s.exit) | (s.scrollBackground ? 0x80u : 0u)),
                        static_cast<std::uint8_t>(formation & 0xFF), static_cast<std::uint8_t>(formation >> 8)};
            } else if constexpr (std::is_same_v<T, ai::Dialog>) {
                return {0xF3, static_cast<std::uint8_t>(s.dialog & 0xFF), static_cast<std::uint8_t>(s.dialog >> 8)};
            } else if constexpr (std::is_same_v<T, ai::CommandOneOf>) {
                return {0xF4, choice(s.choices[0]), choice(s.choices[1]), choice(s.choices[2])};
            } else if constexpr (std::is_same_v<T, ai::ShowOrHideMonsters>) {
                return {0xF5, byteOf(s.type), byteOf(s.mode), s.monsters.slots.bits};
            } else if constexpr (std::is_same_v<T, ai::ItemOneOf>) {
                return {0xF6, static_cast<std::uint8_t>(s.thrown ? 1 : 0), choice(s.twoInThree),
                        choice(s.oneInThree)};
            } else if constexpr (std::is_same_v<T, ai::BattleEvent>) {
                return {0xF7, s.event};
            } else if constexpr (std::is_same_v<T, ai::ChangeVariable>) {
                const std::uint8_t operation = s.change == AiVariableChange::Set   ? 0x00
                                               : s.change == AiVariableChange::Add ? 0x80
                                                                                   : 0xC0;
                return {0xF8, variable(s.variable), static_cast<std::uint8_t>(s.amount | operation)};
            } else if constexpr (std::is_same_v<T, ai::ChangeSwitch>) {
                const SwitchBytes bytes = switchBytes(s.which);
                return {0xF9, byteOf(s.change), bytes.variable, bytes.bit};
            } else if constexpr (std::is_same_v<T, ai::Animate>) {
                if (s.animation == MonsterAnimation::SoundEffect) {
                    return {0xFA, byteOf(s.animation), s.pan, s.sound};
                }
                return {0xFA, byteOf(s.animation), s.monsters.slots.bits, 0};
            } else if constexpr (std::is_same_v<T, ai::Effect>) {
                std::uint8_t operand = 0;
                switch (s.effect) {
                    case AiEffect::MakeInvincible:
                    case AiEffect::EndInvincible:
                    case AiEffect::MakeTargetable:
                    case AiEffect::MakeUntargetable:
                    case AiEffect::FillGauge:
                        operand = target(s.target);
                        break;
                    case AiEffect::SetStatus:
                    case AiEffect::ClearStatus:
                        operand = byteOf(s.status);
                        break;
                    default:
                        break;
                }
                return {0xFB, byteOf(s.effect), operand};
            } else if constexpr (std::is_same_v<T, AiCondition>) {
                const ConditionBytes bytes = condition(s);
                return {0xFC, bytes.number, bytes.first, bytes.second};
            } else if constexpr (std::is_same_v<T, ai::Wait>) {
                return {0xFD};
            } else if constexpr (std::is_same_v<T, ai::EndIf>) {
                return {0xFE};
            } else {
                static_assert(std::is_same_v<T, ai::End>, "every step has its bytes");
                return {0xFF};
            }
        },
        step.kind);
}

// A part's bytes: every step's, in order.
inline std::vector<std::uint8_t> aiPartBytes(std::span<const AiStep> steps) {
    std::vector<std::uint8_t> bytes;
    for (const AiStep& step : steps) {
        const std::vector<std::uint8_t> one = aiStepBytes(step);
        bytes.insert(bytes.end(), one.begin(), one.end());
    }
    return bytes;
}

// How many bytes the first `count` steps of a part take: where step `count` starts.
inline std::size_t aiStepOffset(std::span<const AiStep> steps, std::size_t count) {
    return aiPartBytes(steps.first(count)).size();
}

}  // namespace ostinato::test
