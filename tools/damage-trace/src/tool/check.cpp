// runCheck() and runReplay(): the sweeps run and logged to check.log, and a trace's calls and
// endings replayed against the port and logged to replay.log.

#include "tool/check.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/cartridge.h"
#include "battle/ai.h"
#include "battle/damage.h"
#include "data/ai_scripts.h"
#include "oracle/damage_oracle.h"
#include "scenarios/battle/start_draw.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

namespace {

// A line for each cell the last call read before anything wrote it, past the cells listed as left as
// the machine has them.
std::string unwrittenReadLines(const trace::DamageOracle& oracle) {
    std::string lines;
    for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
        if (!trace::leftAsTheMachineHasIt(read.at)) {
            char text[64];
            std::snprintf(text, sizeof text, "read before written: $%02X:%04X (%02X)\n", (read.at >> 16) & 0xFF,
                          read.at & 0xFFFF, read.value);
            lines += text;
        }
    }
    return lines;
}

std::string numberDifference(long port, long cartridge) {
    return port == cartridge ? std::string{} : "port " + std::to_string(port) + ", cartridge " + std::to_string(cartridge) + "\n";
}

std::string numberAnswers(long port, long cartridge) {
    return " port=" + std::to_string(port) + " cartridge=" + std::to_string(cartridge);
}

}  // namespace

constexpr std::array<Sweep, 35> kSweeps{{
    {.name = "base-damage",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawScenario(rng);
         const std::uint16_t port = s.attack.record.traits.has(AttackTrait::PHYSICAL)
                                        ? battle::physicalDamage(s.state, s.attack, s.attacker)
                                        : battle::magicDamage(s.attack.record.power, s.attack.attackerStat, s.attack.attackerLevel);
         const std::uint16_t guest = oracle.calcDamage(s);
         return DrawResult{trace::describe(s), numberAnswers(port, guest), numberDifference(port, guest)};
     }},
    {.name = "relic-bonus",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawScenario(rng);
         const auto damage = static_cast<std::uint16_t>(s.attack.damage);
         const std::uint16_t port = battle::relicBonus(s.state, s.attack, s.attacker, damage);
         const std::uint16_t guest = oracle.relicBonus(s, damage);
         return DrawResult{trace::describe(s), numberAnswers(port, guest), numberDifference(port, guest)};
     }},
    {.name = "multiplier",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         trace::DamageScenario s = trace::drawScenario(rng);
         const trace::MultiplierResult guest = oracle.applyMultiplier(s);
         battle::scaleDamage(s.attack);
         return DrawResult{trace::describe(s), numberAnswers(s.attack.damage, guest.damage),
                           numberDifference(s.attack.damage, guest.damage) +
                               numberDifference(s.attack.multiplier, guest.stepsLeft)};
     }},
    {.name = "target-damage",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::TargetScenario d = trace::drawTargetScenario(rng);
         return DrawResult{trace::describe(d), "",
                           trace::differences(trace::portTargetDamage(d.scenario, d.target),
                                              oracle.targetDamage(d.scenario, d.target))};
     }},
    {.name = "apply-damage",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawApplyScenario(rng);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portApplyDamage(s), oracle.applyDamage(s))};
     }},
    {.name = "reflected-attack",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawReflectScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portReflectedAttack(s), oracle.reflectedAttack(s))};
     }},
    {.name = "status-changes",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawStatusChangesScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portStatusChanges(s), oracle.statusChanges(s))};
     }},
    {.name = "status-effect",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::TargetScenario d = trace::drawStatusEffectScenario(rng);
         return DrawResult{trace::describe(d), "",
                           trace::differences(trace::portStatusEffect(d.scenario, d.target),
                                              oracle.statusEffect(d.scenario, d.target))};
     }},
    {.name = "attacker-effect",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawAttackerEffectScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::attackerEffectResult(trace::runAttackerEffect(s)),
                                              oracle.attackerEffect(s))};
     }},
    {.name = "target-effect",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::TargetScenario d = trace::drawTargetEffectScenario(rng);
         return DrawResult{trace::describe(d), "",
                           trace::differences(trace::targetEffectResult(trace::runTargetEffect(d.scenario, d.target)),
                                              oracle.targetEffect(d.scenario, d.target)),
                           d.target};
     }},
    {.name = "params",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawAttackerEffectScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portInitParams(s), oracle.initParams(s))};
     }},
    {.name = "cover",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawCoverScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portCoverTarget(s), oracle.coverEffect(s))};
     }},
    {.name = "esper-learning",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RecordScenario s = trace::drawLearningScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portLearnFromEsper(s), oracle.learnFromEsper(s))};
     }},
    {.name = "level-up",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RecordScenario s = trace::drawLevelUpScenario(rng);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portLevelUp(s), oracle.levelUp(s))};
     }},
    {.name = "ai-targets",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::AiTargetsCase c = trace::drawAiTargetsCase(rng);
         return DrawResult{trace::describe(c.scenario), "",
                           trace::differences(trace::portAiTargets(c.scenario, c.target, c.reachableOnly),
                                              oracle.aiTargets(c.scenario, c.target, c.reachableOnly))};
     }},
    {.name = "ai-conditions",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::AiConditionCase c = trace::drawAiConditionCase(rng);
         return DrawResult{trace::describe(c.scenario), "",
                           trace::differences(trace::portAiCondition(c.scenario, c.condition),
                                              oracle.aiCondition(c.scenario, c.condition))};
     }},
    {.name = "ai-setup",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         // A script readied into a slot that already held one, both drawn.
         const auto slot = battle::BattlerSlot::monster(static_cast<int>(rng() % battle::kMonsterSlots));
         trace::AiScenario s =
             trace::drawAiRunScenario(rng, static_cast<std::uint16_t>(rng() % kAiScriptCount), slot);
         s.state.at(slot).aiScript = static_cast<std::uint16_t>(rng() % kAiScriptCount);
         const trace::AiScriptPlaces guest = oracle.readyAi(s, slot);
         battle::readyAi(s.state, slot);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portScriptPlaces(s.state, slot), guest)};
     }},
    {.name = "ai-turns",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const auto actor = battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
         const trace::AiScenario s =
             trace::drawAiRunScenario(rng, static_cast<std::uint16_t>(rng() % kAiScriptCount), actor);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portAiTurn(s), oracle.aiTurn(s))};
     }},
    {.name = "ai-counters",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         std::uint16_t script = 0;
         do {
             script = static_cast<std::uint16_t>(rng() % kAiScriptCount);
         } while (!aiScript(script).hasCounter());
         const auto guard = static_cast<trace::CounterGuard>(rng() % trace::kCounterGuards);
         const trace::AiScenario s = trace::drawAiRunScenario(
             rng, script, battle::BattlerSlot::monster(static_cast<int>(rng() % battle::kMonsterSlots)), guard);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portAiCounter(s), oracle.aiCounter(s))};
     }},
    {.name = "ai-commands",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::AiCommandCase c = trace::drawAiCommandCase(rng);
         return DrawResult{trace::describe(c.scenario) + " " + trace::listedActionText(c.action), "",
                           trace::differences(trace::portAiCommand(c.scenario, c.action),
                                              oracle.aiCommand(c.scenario, c.action))};
     }},
    {.name = "attack",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawAttackScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::attackResult(trace::portAttack(s)), oracle.attack(s))};
     }},
    {.name = "self-attack",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::DamageScenario s = trace::drawSelfAttackScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::attackResult(trace::portSelfAttack(s)), oracle.selfAttack(s))};
     }},
    {.name = "own-actions",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::OwnActionCase c = trace::drawOwnActionCase(rng);
         return DrawResult{trace::describe(c), "",
                           trace::differences(trace::portOwnAction(c.scenario, c.action),
                                              oracle.ownAction(c.scenario, c.action))};
     }},
    {.name = "add-exp",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RewardScenario s = trace::drawExperienceScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portAddExperience(s), oracle.addExperience(s))};
     }},
    {.name = "item-learning",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RewardScenario s = trace::drawEquipmentScenario(rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portLearnFromEquipment(s), oracle.learnFromEquipment(s))};
     }},
    {.name = "level-ups",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RewardScenario s = trace::drawLevelScenario(rng);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portLevelUps(s), oracle.levelUps(s))};
     }},
    {.name = "abilities",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const auto actor = static_cast<CharacterPropId>(rng() % 16);
         const auto level = static_cast<std::uint8_t>(1 + rng() % 99);
         const trace::RewardScenario s = trace::abilityCase(actor, level, static_cast<int>(rng() % 3), rng);
         return DrawResult{trace::describe(s), "",
                           trace::differences(trace::portLearnAbilities(s), oracle.learnAbilities(s))};
     }},
    {.name = "rages",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::RewardScenario s = trace::drawRageScenario(rng);
         return DrawResult{trace::describe(s), "", trace::differences(trace::portLearnRages(s), oracle.learnRages(s))};
     }},
    {.name = "items-obtained",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::ItemsObtainedScenario s = trace::drawItemsObtainedScenario(rng);
         return DrawResult{"item " + std::to_string(static_cast<int>(s.item)) + " x" + std::to_string(s.quantity), "",
                           trace::differences(trace::portAddItemObtained(s), oracle.addItemObtained(s))};
     }},
    {.name = "items-merged",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::ItemsObtainedScenario s = trace::drawItemsObtainedScenario(rng);
         return DrawResult{"added " + std::to_string(s.list.added) + " merged " + std::to_string(s.list.merged), "",
                           trace::differences(trace::portMergeItemsObtained(s), oracle.mergeItemsObtained(s))};
     }},
    {.name = "wrap-up",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::EndingScenario s = trace::drawEndingScenario(rng);
         std::string differences = trace::differences(trace::portWrapUp(s), oracle.wrapUp(s));
         differences += unwrittenReadLines(oracle);
         return DrawResult{trace::describe(s), "", differences};
     }},
    {.name = "win",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::EndingScenario s = trace::drawWinScenario(rng);
         std::string differences = trace::differences(trace::portWin(s), oracle.win(s));
         differences += unwrittenReadLines(oracle);
         return DrawResult{trace::describe(s), "", differences};
     }},
    {.name = "loss",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::EndingScenario s = trace::drawWinScenario(rng);
         // The annihilated message, Banon's, or none.
         const std::optional<std::uint8_t> message =
             rng() % 3 == 0 ? std::nullopt
                            : std::optional<std::uint8_t>(rng() % 2 == 0 ? battle::kMessageAnnihilated
                                                                         : battle::kMessageBanonFell);
         std::string differences = trace::differences(trace::portLoss(s, message), oracle.loss(s, message));
         differences += unwrittenReadLines(oracle);
         return DrawResult{trace::describe(s), "", differences};
     }},
    {.name = "write-back",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::EndingScenario s = trace::drawWinScenario(rng);
         std::string differences = trace::differences(trace::portWriteBack(s), oracle.writeBack(s));
         differences += unwrittenReadLines(oracle);
         return DrawResult{trace::describe(s), "", differences};
     }},
    {.name = "start",
     .draw = [](trace::DamageOracle& oracle, std::mt19937& rng) {
         const trace::StartScenario s = trace::drawStartScenario(rng, oracle.text(), oracle.monsterSprites());
         trace::StartCheck check = oracle.checkStart(s);
         std::vector<std::string_view> reached;
         if (check.cartridge.reachedTheLoop) {
             reached = trace::startReached(s, check.port, check.cartridge.cells);
         }
         return DrawResult{.described = trace::describe(s),
                           .answers = "",
                           .differences = std::move(check.differences),
                           .reached = std::move(reached)};
     },
     .reach = trace::startReachRows},
}};

int runCheck(std::span<const std::uint8_t> image, const std::filesystem::path& directory, const CheckOptions& options) {
    std::ofstream log{directory / "check.log"};
    int mismatches = 0;
    int sweeps = 0;
    int unreached = 0;
    // The game's text and the monsters' sprites, which the start's sweep reads.
    const assets::IngestedContent content = assets::ingestCartridge(image);
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image};
        oracle.start();
        oracle.trace(options.replayTraced);
        oracle.useText(content.text);
        oracle.useMonsterSprites(content.monsterSprites);
        for (const Sweep& sweep : kSweeps) {
            if (!options.only.empty() && std::find(options.only.begin(), options.only.end(), sweep.name) == options.only.end()) {
                continue;
            }
            ++sweeps;
            std::mt19937 rng{options.seed};
            std::map<std::string_view, int> reachCounts;
            for (int draw = 0; draw < options.draws; ++draw) {
                DrawResult result = sweep.draw(oracle, rng);
                for (const std::string_view row : result.reached) {
                    ++reachCounts[row];
                }
                if (options.replayTraced) {
                    std::string text;
                    for (const std::string& line : oracle.takeTrace()) {
                        text += line + '\n';
                    }
                    std::istringstream traced{text};
                    for (trace::TracedCall call : trace::parseTrace(traced)) {
                        if (!call.target) {
                            call.target = result.target;
                        }
                        const std::string parted = oracle.replay(call);
                        if (!parted.empty()) {
                            result.differences += "replayed " + call.routine + ":\n" + parted;
                        }
                    }
                    std::istringstream endings{text};
                    for (const trace::TracedEnding& ending : trace::parseEndings(endings)) {
                        const std::string parted = oracle.replayEnding(ending);
                        if (!parted.empty()) {
                            result.differences += "replayed " + ending.routine + ":\n" + parted;
                        }
                    }
                }
                const bool same = result.differences.empty();
                log << sweep.name << ' ' << draw << ": " << result.described << result.answers
                    << (same ? " agrees\n" : " MISMATCH\n") << result.differences;
                if (!same) {
                    ++mismatches;
                    std::printf("%s draw %d: %s\n%s", std::string{sweep.name}.c_str(), draw, result.described.c_str(),
                                result.differences.c_str());
                }
            }
            if (sweep.reach != nullptr) {
                std::printf("%s, what the %d draws reached:\n", std::string{sweep.name}.c_str(), options.draws);
                for (const std::string_view row : sweep.reach()) {
                    const int count = reachCounts[row];
                    std::printf("  %6d  %s%s\n", count, std::string{row}.c_str(), count == 0 ? "  NOT REACHED" : "");
                    log << sweep.name << " reached " << row << ": " << count << '\n';
                    if (count == 0) {
                        ++unreached;
                    }
                }
            }
        }
    });
    std::printf("%d draws through each of %d routines: %d mismatch%s; %s\n", options.draws, sweeps, mismatches,
                mismatches == 1 ? "" : "es", (directory / "check.log").string().c_str());
    if (unreached != 0) {
        std::printf("%d row%s of a reach table no draw reached; the tests' sweeps draw enough to reach every one\n",
                    unreached, unreached == 1 ? "" : "s");
    }
    return mismatches == 0 ? 0 : 1;
}

int runReplay(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
              const std::filesystem::path& recording) {
    std::ifstream recorded{recording};
    if (!recorded) {
        std::fprintf(stderr, "%s could not be read\n", recording.string().c_str());
        return 1;
    }
    const std::vector<trace::TracedCall> calls = trace::parseTrace(recorded);
    recorded.clear();
    recorded.seekg(0);
    const std::vector<trace::TracedEnding> endings = trace::parseEndings(recorded);
    std::ofstream log{directory / "replay.log"};
    int parted = 0;
    const auto logOne = [&](std::uint64_t frame, const std::string& routine, const std::string& differences) {
        log << "f" << frame << ' ' << routine << (differences.empty() ? " agrees\n" : " PARTS\n") << differences;
        if (!differences.empty()) {
            ++parted;
            std::printf("frame %llu, %s:\n%s", static_cast<unsigned long long>(frame), routine.c_str(),
                        differences.c_str());
        }
    };
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image};
        oracle.start();
        for (const trace::TracedCall& call : calls) {
            std::string differences;
            try {
                differences = oracle.replay(call);
            } catch (const std::runtime_error& error) {
                differences = std::string{"could not be rebuilt: "} + error.what() + "\n";
            }
            logOne(call.frame, call.routine, differences);
        }
        for (const trace::TracedEnding& ending : endings) {
            std::string differences;
            try {
                differences = oracle.replayEnding(ending);
            } catch (const std::runtime_error& error) {
                differences = std::string{"could not be rebuilt: "} + error.what() + "\n";
            }
            logOne(ending.frame, ending.routine, differences);
        }
    });
    std::printf("%zu calls and %zu endings replayed, %d differing from the cartridge; %s\n", calls.size(),
                endings.size(), parted, (directory / "replay.log").string().c_str());
    return parted == 0 ? 0 : 1;
}

}  // namespace damage_trace
