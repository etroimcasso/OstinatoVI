// A battler's AI script wired into the battle (battle/command_executor.h, battle/battle_start.h,
// docs/contracts/battle_ai.md § A): CommandExecutor's turn, counterattack and setup run the script, and
// a scripted battle's party member has its script readied as it is seated. The AI's routines followed
// by the damage trace, the cartridge's own calls traced and replayed against the port, are in
// tests/oracle/test_ai_trace.cpp.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ai_battle.h"
#include "ai_trace_cases.h"
#include "battle/ai.h"
#include "battle/automatic_actions.h"
#include "battle/battle_hooks.h"
#include "battle/battle_start.h"
#include "battle/command_executor.h"
#include "data/ai_scripts.h"
#include "data/formations.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/status_id.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using trace::AiRunResult;
using trace::AiScenario;

// The Veldt's setup gives Gau the 115th of them (party_setup's table, char_ai.asm).
constexpr std::uint16_t kGauVeldtScript = kCharacterAiScripts + 114;

// A monster-name table of 0xFF bytes and no other text: what loading a monster reads.
const TextCorpus& blankText() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(3840, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

const MonsterSprites& noSprites() {
    static const MonsterSprites sprites;
    return sprites;
}

// The actions a battler's lists hold, as the cartridge's lists would.
std::vector<std::string> listed(const std::deque<battle::Action>& actions) {
    std::vector<std::string> out;
    for (const battle::Action& action : actions) {
        out.push_back(trace::listedActionText(action));
    }
    return out;
}

AiScenario withScript(AiScenario s, battle::BattlerSlot slot, std::uint16_t script) {
    s.state.at(slot).aiScript = script;
    battle::readyAi(s.state, slot);
    return s;
}

// ─── the seams ──────────────────────────────────────────────────────────────────────────────────

TEST(AiSeams, AMonstersTurnReadsItsScript) {
    // ExecAction, :211-224: a monster that chooses for itself runs its script's turn part.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    const AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::LOBO));
    battle::BattleState wired = s.state;
    battle::BattleState direct = s.state;
    executor.runAi(wired, kSelf);
    battle::runAiTurn(direct, executor, kSelf);
    ASSERT_FALSE(wired.at(kSelf).actions.empty());
    EXPECT_EQ(listed(wired.at(kSelf).actions), listed(direct.at(kSelf).actions));
    EXPECT_EQ(wired.rng.position(), direct.rng.position());
}

TEST(AiSeams, AConfusedMonsterPicksItsOwnAttackAndLeavesItsScriptAlone) {
    // :214-222: charmed, berserk, confused or in the colosseum, a monster uses one of its own
    // attacks (RandMonsterAction) instead of reading its script.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::LOBO));
    s.state.at(kSelf).status.set(StatusId::CONFUSE);
    battle::BattleState wired = s.state;
    battle::BattleState own = s.state;
    executor.runAi(wired, kSelf);
    battle::chooseMonsterAction(own, executor, kSelf);
    EXPECT_EQ(listed(wired.at(kSelf).actions), listed(own.at(kSelf).actions));
    EXPECT_EQ(wired.at(kSelf).turnResume.blocksSkipped, s.state.at(kSelf).turnResume.blocksSkipped);
    EXPECT_EQ(wired.rng.position(), own.rng.position());
}

TEST(AiSeams, AScriptedPartyMemberReadsItsScriptWhateverItsStatus) {
    // QueueAction, :509-512: a party member with a script runs it when its gauge fills; Confuse,
    // Berserk and Charm do not divert it.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = withScript(aiBattle(), kTerra, kGauVeldtScript);
    s.state.at(kTerra).status.set(StatusId::CONFUSE);
    battle::BattleState wired = s.state;
    battle::BattleState direct = s.state;
    executor.runAi(wired, kTerra);
    battle::runAiTurn(direct, executor, kTerra);
    ASSERT_FALSE(wired.at(kTerra).actions.empty());
    EXPECT_EQ(listed(wired.at(kTerra).actions), listed(direct.at(kTerra).actions));
}

TEST(AiSeams, ACounterattackReadsTheCounterPartAimedAtTheBattlerItAnswers) {
    // ExecRetal, :12416-12425, and ExecAI, :4218-4223: the battler answered is where the run's aim
    // starts. Mag Roader's fourth script answers with Wild Cat's turn part: with more than one
    // monster standing it skips the Fire Ball block and attacks, aimed where the run is.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::MAG_ROADER_4));
    s.state.counterattackRunning = true;
    battle::BattleState wired = s.state;
    battle::BattleState direct = s.state;
    executor.runCounterAi(wired, kSelf, battle::BattlerSet::of(kTerra));
    battle::runAiCounter(direct, executor, kSelf, battle::BattlerSet::of(kTerra));
    EXPECT_TRUE(wired.counterAiRanThisStep.has(kSelf));
    ASSERT_EQ(wired.at(kSelf).counterActions.size(), 1u);
    const auto* answer = std::get_if<battle::CommandAction>(&wired.at(kSelf).counterActions.front());
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->targets, battle::BattlerSet::of(kTerra));
    EXPECT_EQ(listed(wired.at(kSelf).counterActions), listed(direct.at(kSelf).counterActions));
    EXPECT_EQ(wired.rng.position(), direct.rng.position());
}

TEST(AiSeams, LoadingAMonsterReadiesItsScript) {
    // LoadMonsterProp, :7313: InitAI with the monster's index.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = aiBattle();
    s.state.at(kSelf).aiScript = static_cast<std::uint16_t>(MonsterId::REPO_MAN);
    executor.initAi(s.state, kSelf);
    const AiScript& script = aiScript(static_cast<std::uint16_t>(MonsterId::REPO_MAN));
    EXPECT_EQ(s.state.at(kSelf).turnScript.data(), script.turn.data());
    EXPECT_EQ(s.state.at(kSelf).counterScript.data(), script.counter.data());
    EXPECT_TRUE(s.state.at(kSelf).hasCounterScript);
}

TEST(AiSeams, AScriptedBattleSeatsAPartyMemberWithItsScriptReadied) {
    // InitBattle, :7835-7840: a scripted setup's party member has InitAI called as it is seated,
    // before the monsters are loaded. The Veldt's battle seats Gau with his script.
    FormationId veldt{};
    bool found = false;
    for (const FormationEntry& entry : formations()) {
        const FormationAux& aux = getFormationAux(entry.id);
        if (aux.characterAiEnabled() && static_cast<CharAiId>(aux.characterAiIndex()) == CharAiId::GAU_VELDT) {
            veldt = entry.id;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
    battle::BattleState party = battleWithParty({CharacterPropId::SABIN, CharacterPropId::CYAN});
    party.party.characters[7] = startingRecord(CharacterPropId::GAU);
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    const battle::BattleState state = battle::startBattle(battle::BattleStart{.formation = FormationRef::of(veldt)},
                                                          party.party, {}, blankText(), noSprites(), executor);
    const battle::BattlerSlot gau = battle::BattlerSlot::character(2);
    ASSERT_EQ(state.at(gau).actor, CharacterPropId::GAU);
    ASSERT_EQ(state.at(gau).aiScript, kGauVeldtScript);
    const AiScript& script = aiScript(kGauVeldtScript);
    EXPECT_EQ(state.at(gau).turnScript.data(), script.turn.data());
    EXPECT_EQ(state.at(gau).turnScript.size(), script.turn.size());
    EXPECT_EQ(state.at(gau).hasCounterScript, script.hasCounter());
}

}  // namespace
}  // namespace ostinato::test
