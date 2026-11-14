// The cartridge's setting up of the attack's animation and its cover, run over a scenario.

#include "oracle/damage_oracle.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/attack.h"
#include "battle/battle_script.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "oracle/routines.h"
#include "oracle/status/attacker_effects_shared.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/report_text.h"
#include "scenarios/status/attacker_effects_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// The ally a cover stepped in front of, by its slot index, and the byte after it. The cover shifts
// the two as one word, so the byte after is written as every attack starts it (ClearGfxParams): 0.
constexpr std::uint32_t kCoveredAlly = kWorkRam + 0x00A8;
constexpr std::uint32_t kAfterCoveredAlly = kWorkRam + 0x00A9;
}  // namespace

ParamsInitResult DamageOracle::initParams(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    call([&] { initGfxParams_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    EffectCells out;
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    const std::uint8_t rules = readByte(kAimingRules);
    out.put("random target", (rules & kRandomTargetBit) != 0);
    out.put("fall back to earlier targets", (rules & kFallBackToEarlierTargetsBit) != 0);
    out.put("other side only", (rules & kOpposingSideBit) != 0);
    out.put("swirl suppressed", (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0);
    return ParamsInitResult{.cells = std::move(out.cells)};
}

CoverResult DamageOracle::coverEffect(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    writeTargetEffectCells(scenario);
    // The battlers stepping in are marked in the same word as the battlers a spell bounced off.
    const battle::AttackAnimationParams& params = scenario.attack.params;
    writeWord(kParamsReflectedOff, maskOf(params.reflectedOff | params.steppingIn));
    writeByte(kCoveredAlly,
              params.coveredAlly ? static_cast<std::uint8_t>(params.coveredAlly->index()) : kNoOne);
    writeByte(kAfterCoveredAlly, 0);
    call([&] {
        coverEffect_(slotIndex(scenario.attacker), kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    EffectCells out;
    out.put("targets", std::to_string(readWord(kTargets)));
    const std::uint8_t covered = readByte(kCoveredAlly);
    out.put("covered ally", covered == kNoOne ? std::string{"none"} : std::to_string(covered));
    out.put("stepping in", std::to_string(readWord(kParamsReflectedOff)));
    return CoverResult{.cells = std::move(out.cells)};
}

}  // namespace ostinato::trace
