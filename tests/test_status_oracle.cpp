// A petrified battler is marked as having died on every pass the cartridge's status routine makes,
// not only the first: worked out on the port by hand, and on the cartridge beside it where the run
// asks it (tests/with_cartridge.h). The status routines on the oracle, and their traced calls
// replayed, are in tests/oracle/test_status_oracle.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>

#include "battle/status.h"
#include "oracle/damage_oracle.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "status_oracle_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using trace::DamageScenario;
using trace::StatusChangesResult;

// The cartridge's status pass over `scenario`, where the run asks it.
std::optional<StatusChangesResult> cartridgeStatusChanges(const Cartridge& cartridge, const DamageScenario& scenario) {
    if (!cartridge) {
        return std::nullopt;
    }
    return cartridge.oracle().statusChanges(scenario);
}

// The cartridge's answer, where there is one, marks `monster` as having died this action.
void expectTheCartridgeMarksItDying(const std::optional<StatusChangesResult>& guest, BattlerSlot monster) {
    if (guest) {
        EXPECT_EQ(guest->diedAwaitingCounter, BattlerSet::of(monster));
    }
}

// `answer` has `monster` petrified and marked as having died this action.
void expectPetrifiedAndDying(const StatusChangesResult& answer, BattlerSlot monster) {
    EXPECT_EQ(answer.status[static_cast<std::size_t>(monster.index())].bytes, StatusSet::of(StatusId::PETRIFY).bytes);
    EXPECT_EQ(answer.diedAwaitingCounter, BattlerSet::of(monster));
}

// The port's second pass, and the cartridge's where there is one: the two agree, and each has the
// monster petrified and dying.
void expectTheSecondPass(const StatusChangesResult& port, const std::optional<StatusChangesResult>& guest,
                         BattlerSlot monster) {
    expectPetrifiedAndDying(port, monster);
    if (guest) {
        EXPECT_EQ(trace::differences(port, *guest), "");
        expectPetrifiedAndDying(*guest, monster);
    }
}

// A petrified monster hit by two attacks in a row: the battle's counterattack step takes it out of
// "died this action" in between, and the second pass puts it back, on the cartridge as in the port.
TEST_F(StatusOracleTest, APetrifiedMonsterIsMarkedAsDyingOnEveryPass) {
    const BattlerSlot monster = BattlerSlot::monster(0);
    DamageScenario scenario;
    battle::Battler& battler = scenario.state.at(monster);
    battler.present = true;
    battler.maxHp = 2000;
    battler.hp = 2000;
    battler.statusesToSet = StatusSet::of(StatusId::PETRIFY);
    withCartridge(image(), [&](const Cartridge& cartridge) {
        const std::optional<StatusChangesResult> first = cartridgeStatusChanges(cartridge, scenario);
        battle::applyStatusChanges(scenario.state);
        expectTheCartridgeMarksItDying(first, monster);
        EXPECT_EQ(scenario.state.diedAwaitingCounter, BattlerSet::of(monster));

        scenario.state.diedAwaitingCounter = {};
        const std::optional<StatusChangesResult> second = cartridgeStatusChanges(cartridge, scenario);
        expectTheSecondPass(trace::portStatusChanges(scenario), second, monster);
    });
}

}  // namespace
}  // namespace ostinato::test
