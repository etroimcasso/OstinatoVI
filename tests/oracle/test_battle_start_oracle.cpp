// A battle's start on the oracle (docs/contracts/battle_core.md § A battle's start): the battle
// program's entry is where the US cartridges hold it and an image without it is refused; the program is
// called whole, as the field calls it, from the first fight in Narshe as first-fight.inputs leaves the
// machine and from that fight's scenario written into a machine fresh from its reset code, and in both
// it opens, reaches the loop's first frame wait and returns; and a start scenario written reads back
// the same.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/action_queue.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "data/battle_tables.h"
#include "data/char_ai.h"
#include "data/formations.h"
#include "data/rng_table.h"
#include "game/character_record.h"
#include "machine/machine_state.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "oracle/damage_oracle.h"
#include "cells/battle/state.h"
#include "cells/damage/per_target.h"
#include "cells/ending/ending.h"
#include "cells/status/effects.h"
#include "scenarios/battle/start_draw.h"
#include "retropp/guest_escape.h"
#include "retropp/snes.h"
#include "retropp/vm.h"
#include "vanilla_rom.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

namespace fs = std::filesystem;
namespace cells = trace::cells;
using trace::DamageOracle;
using trace::StartResult;

// The battle program's entry, as the field's jump to it lands (battle/battle_main.asm:39-40).
constexpr std::uint32_t kBattleEntry = 0xC20000;
// How far past first-fight.inputs's last line a run looks for its battle.
constexpr std::uint64_t kRunOutFrames = 600;
// The formations, and a scripted setup's reach byte that leaves monster slot 0 out.
constexpr int kFormationCount = 576;
constexpr std::uint8_t kSlotZeroOutOfReach = 0xFE;
// Places in a StartResult's cells: the play clock's frames, the monsters standing, and the monsters
// the AI leaves in reach.
constexpr std::size_t kPlayClockFramesCell = 0x021E;
constexpr std::size_t kMonstersStandingCell = 0x2F2F;
constexpr std::size_t kMonsterReachCell = 0x2F46;
// Cells for the gates: one the comparison holds whole (the party slot 0's field flags, $3AA0), one
// no field determines and no row of the cells not compared lists (one of the field program's own,
// $0600), and one the comparison leaves out (which way the battlers face, $2F50).
constexpr std::uint32_t kWorkRam = 0x7E0000;
constexpr std::uint32_t kHeldCell = kWorkRam + 0x3AA0;
constexpr std::uint32_t kUncoveredCell = kWorkRam + 0x0600;
constexpr std::uint32_t kLeftOutCell = kWorkRam + 0x2F50;
// The byte after the last inventory slot's quantity, which the start reads sixteen bits wide.
constexpr std::uint32_t kQuantityHighByte = kWorkRam + 0x1A69;
// The play clock's frames run 1 to 60.
constexpr int kPlayClockFramesMost = 60;
// The battle types as $201F holds them (ChooseBattleType :7575-7577) and the formation's possible
// types (FormationAux::possibleBattleTypes()).
constexpr std::uint8_t kFrontAttack = 0;
constexpr std::uint8_t kBackAttack = 1;
constexpr std::uint8_t kPincerAttack = 2;
constexpr std::uint8_t kOnlyAFrontAttack = 0x10;
// $B1 bit 5, a back attack for the battle (InitBattleType_01 :7664-7665).
constexpr std::uint8_t kBackAttackBit = 0x20;
// The opening's battle messages (InitBattle :6138-6161), each an interrupting action of command $25
// with the message as its attack (CreateImmediateAction :12881-12894).
constexpr std::uint8_t kBattleMessageCommand = 0x25;
constexpr std::uint8_t kPreemptiveMessage = 0x22;
constexpr std::uint8_t kBackAttackMessage = 0x23;
constexpr std::uint8_t kPincerMessage = 0x25;
constexpr std::uint8_t kNoSlot = 0xFF;
// A record's offset where the start found none for a party slot; a record is $25 bytes.
constexpr std::uint16_t kNoRecordOffset = 0xFFFF;
constexpr std::uint16_t kRecordBytes = 0x25;
// The colosseum's last formation.
constexpr FormationId kColosseumFormation = FormationId::PUGS;
// How many drawn starts the sweep checks: as many as twenty seconds hold on the machine they were
// measured on (1,300 in 20.0 s), and never fewer than 100.
constexpr int kDrawnStarts = 1300;

class BattleStartOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        image_ = new std::vector<std::uint8_t>{vanillaRom()};
        content_ = new assets::IngestedContent{assets::ingestCartridge(image())};
    }
    static void TearDownTestSuite() {
        delete content_;
        content_ = nullptr;
        delete image_;
        image_ = nullptr;
        delete firstFight_;
        firstFight_ = nullptr;
    }

    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The machine on the tick before the first fight in Narshe's battle program is entered, played from
    // power-on by first-fight.inputs.
    static const std::vector<std::uint8_t>& firstFight() {
        if (firstFight_ == nullptr) {
            std::ifstream inputs{fs::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / "../../../tools/damage-trace/inputs/first-fight.inputs"};
            if (!inputs) {
                throw std::runtime_error("first-fight.inputs could not be read");
            }
            const std::vector<trace::InputStep> script = trace::readInputScript(inputs);
            const std::uint64_t lastFrame = script.back().frame + kRunOutFrames;
            firstFight_ = new std::vector<std::uint8_t>{};
            trace::runOnLoop([&] {
                retropp::Vm::SNES machine{retropp::VmConfig{}};
                machine.hostRom(image());
                bool entered = false;
                machine.registerEscapes(retropp::escapes(retropp::GuestEscape{
                    .key = "battle entry", .at = kBattleEntry, .handler = [&](retropp::Vm&, std::uint32_t) { entered = true; }}));
                machine.run(retropp::Vm::Advance::OnTick);
                std::vector<std::uint8_t> kept;
                for (std::uint64_t frame = 0; !entered && frame <= lastFrame; ++frame) {
                    machine.buttons(retropp::snes::Ports{.one = trace::buttonsAt(script, frame), .two = std::nullopt});
                    machine.advanceTick();
                    if (!entered) {
                        kept = machine.state();
                    }
                }
                machine.stop();
                if (!entered) {
                    throw std::runtime_error("first-fight.inputs reached no battle");
                }
                *firstFight_ = std::move(kept);
            });
        }
        return *firstFight_;
    }

    static const assets::IngestedContent& content() { return *content_; }

    // The first fight's start, read off the machine as the field leaves it the tick before.
    static trace::StartScenario firstFightScenario() {
        const std::vector<std::uint8_t>& state = firstFight();
        trace::StartScenario scenario;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            if (!oracle.machine().restore(state)) {
                throw std::runtime_error("the first fight's state did not restore");
            }
            scenario = oracle.readStartScenario();
        });
        return scenario;
    }

    // An oracle fresh from the cartridge's reset code, with the text and sprites a start check reads.
    static void ready(DamageOracle& oracle) {
        oracle.start();
        oracle.useText(content().text);
        oracle.useMonsterSprites(content().monsterSprites);
    }

    // The first play clock frames, 1 to 60, whose start the port makes what `wanted` asks for.
    static std::optional<std::uint8_t> framesWhere(trace::StartScenario scenario,
                                                   const std::function<bool(const battle::BattleState&)>& wanted) {
        for (int frames = 1; frames <= kPlayClockFramesMost; ++frames) {
            scenario.start.playTimeFrames = static_cast<std::uint8_t>(frames);
            if (wanted(trace::portStart(scenario, content().text, content().monsterSprites))) {
                return static_cast<std::uint8_t>(frames);
            }
        }
        return std::nullopt;
    }

    // The cartridge's start over `scenario`, checked whole against the port's.
    static trace::StartCheck checked(const trace::StartScenario& scenario) {
        std::optional<trace::StartCheck> check;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            ready(oracle);
            check = oracle.checkStart(scenario);
        });
        return std::move(*check);
    }

    static std::vector<std::uint8_t>* image_;
    static assets::IngestedContent* content_;
    static std::vector<std::uint8_t>* firstFight_;
};

std::vector<std::uint8_t>* BattleStartOracleTest::image_ = nullptr;
assets::IngestedContent* BattleStartOracleTest::content_ = nullptr;
std::vector<std::uint8_t>* BattleStartOracleTest::firstFight_ = nullptr;

// The battle program's entry for the field and BattleMain are where the US cartridges hold them, and
// an image without either is refused.
TEST_F(BattleStartOracleTest, TheBattleProgramsEntryIsWhereTheCartridgeHoldsIt) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::span<const std::uint8_t> cartridge = image();
    EXPECT_EQ(cartridge[0x020000], 0x4C);  // Battle_ext: jmp BattleMain
    EXPECT_EQ(cartridge[0x020001], 0x0C);
    EXPECT_EQ(cartridge[0x020002], 0x00);
    EXPECT_EQ(cartridge[0x02000C], 0x08);  // BattleMain: php; sep #$30
    EXPECT_EQ(cartridge[0x02000D], 0xE2);
    EXPECT_EQ(cartridge[0x02000E], 0x30);
    for (const std::size_t at : {std::size_t{0x020001}, std::size_t{0x02000D}}) {
        std::vector<std::uint8_t> other{cartridge.begin(), cartridge.end()};
        other[at] ^= 0xFF;
        trace::runOnLoop([&] {
            try {
                DamageOracle oracle{other};
                ADD_FAILURE() << "an image without the battle program's entry was taken";
            } catch (const std::runtime_error& refused) {
                const std::string why = refused.what();
                EXPECT_NE(why.find(at == 0x020001 ? "Battle_ext" : "BattleMain"), std::string::npos) << why;
            }
        });
    }
}

// The first fight's start, from the machine as the field left it: the battle opens (graphics command
// $00), reaches the loop's first frame wait ($01), and, ended there, closes ($09) and returns.
TEST_F(BattleStartOracleTest, TheFirstFightReachesTheLoopAndReturns) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const StartResult result = oracle.battleStartAsItStands();
        EXPECT_TRUE(result.reachedTheLoop);
        EXPECT_TRUE(result.returned);
        ASSERT_GE(result.calls.size(), 3u);
        EXPECT_EQ(result.calls[0].command, 0x00);
        EXPECT_EQ(result.calls[1].command, 0x01);
        EXPECT_EQ(result.calls.back().command, 0x09);
    });
}

// The first fight's scenario, read off the machine as the field left it, written into another
// machine and read back: the same scenario.
TEST_F(BattleStartOracleTest, AStartScenarioWrittenReadsBackTheSame) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario read = oracle.readStartScenario();
        EXPECT_EQ(read.start.formation.raw, FormationRef::of(FormationId::GUARD_X2).raw);

        DamageOracle fresh{image()};
        fresh.start();
        fresh.writeStartScenario(read);
        EXPECT_EQ(trace::differences(read, fresh.readStartScenario()), "");
    });
}

// The first fight's scenario written into a machine fresh from its reset code, with nothing else of
// the game run: the battle program opens, reaches the loop's first frame wait and returns.
TEST_F(BattleStartOracleTest, AWrittenScenarioReachesTheLoopAndReturns) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();

        DamageOracle fresh{image()};
        fresh.start();
        fresh.useMonsterSprites(content().monsterSprites);
        const StartResult result = fresh.battleStart(scenario);
        EXPECT_TRUE(result.reachedTheLoop);
        EXPECT_TRUE(result.returned);
        ASSERT_GE(result.calls.size(), 2u);
        EXPECT_EQ(result.calls[0].command, 0x00);
        EXPECT_EQ(result.calls[1].command, 0x01);
    });
}

// The first fight's start, from the machine as the field left it, against the port's start over the
// same scenario: every cell a BattleState field determines the same, the opening's monsters standing
// and the play clock's frames among them, and nothing read beyond the battle's cells that is not
// listed.
TEST_F(BattleStartOracleTest, TheFirstFightsStartIsThePorts) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();
        const StartResult result = oracle.battleStartAsItStands();
        ASSERT_TRUE(result.reachedTheLoop);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(oracle.compareBattleState(port, scenario.cells, result.cells), "");
        EXPECT_EQ(port.monstersStanding.monsters.bits, result.cells[kMonstersStandingCell]) << "the monsters standing";
        EXPECT_EQ(port.playClockFrames, result.cells[kPlayClockFramesCell]) << "the play clock's frames";
        for (const trace::UnwrittenRead& read : result.readsBeyondTheBattlesCells) {
            ADD_FAILURE() << "read beyond the battle's cells: " << std::hex << read.at;
        }
    });
}

// The same start from the first fight's scenario written into a machine fresh from its reset code:
// the port's start again.
TEST_F(BattleStartOracleTest, TheFirstFightsWrittenScenarioStartsAsThePortDoes) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();

        DamageOracle fresh{image()};
        fresh.start();
        fresh.useMonsterSprites(content().monsterSprites);
        const StartResult result = fresh.battleStart(scenario);
        ASSERT_TRUE(result.reachedTheLoop);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(fresh.compareBattleState(port, scenario.cells, result.cells), "");
    });
}

// The first fight's start reads, before anything writes them, only cells left as the machine has
// them; and the quantity's high byte it reads at $1A69 reaches nothing: written $00 and $FF, the
// cells at the loop's first frame wait are the same.
TEST_F(BattleStartOracleTest, TheFirstFightsStartReadsNothingItWasNotHanded) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();

        std::optional<trace::BattleCells> first;
        for (const std::uint8_t quantityHighByte : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
            DamageOracle fresh{image()};
            fresh.start();
            fresh.useMonsterSprites(content().monsterSprites);
            fresh.writeStartScenario(scenario);
            const std::array<std::uint8_t, 1> byte{quantityHighByte};
            fresh.machine().write(retropp::MemoryRegion{.at = kQuantityHighByte, .size = 1}, byte);
            const StartResult result = fresh.battleStartAsItStands();
            ASSERT_TRUE(result.reachedTheLoop);
            for (const trace::UnwrittenRead& read : result.unwrittenReads) {
                if (read.at != kQuantityHighByte) {
                    EXPECT_TRUE(trace::leftAsTheMachineHasIt(read.at)) << std::hex << read.at;
                }
            }
            if (first) {
                for (std::size_t cell = 0; cell < first->size(); ++cell) {
                    if (kWorkRam + cell != kQuantityHighByte) {
                        EXPECT_EQ((*first)[cell], result.cells[cell]) << "$7E:" << std::hex << cell;
                    }
                }
            } else {
                first = result.cells;
            }
        }
    });
}

// The two gates on the first fight's start, each on cells whose answer is known: the write-set gate
// names a cell neither compared nor listed and passes one the comparison holds; the carried-read gate
// names a read of a cell the comparison left out and passes a read of one it holds.
TEST_F(BattleStartOracleTest, TheGatesNameWhatTheComparisonDoesNotHold) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();
        const StartResult result = oracle.battleStartAsItStands();
        ASSERT_TRUE(result.reachedTheLoop);
        ASSERT_TRUE(result.battleWrites.contains(kHeldCell));
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);

        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, {kHeldCell}), "");
        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, {kUncoveredCell}),
                  "$7E:0600 written, bits FF not compared\n");
        // The fill's bits of $3205 are listed; its write is held whole between them.
        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, {kWorkRam + 0x3205}), "");

        EXPECT_EQ(oracle.carriedReads(port, scenario.cells, {trace::UnwrittenRead{.at = kHeldCell, .value = 0}}), "");
        EXPECT_EQ(oracle.carriedReads(port, scenario.cells,
                                      {trace::UnwrittenRead{.at = kLeftOutCell, .value = 0xFF},
                                       trace::UnwrittenRead{.at = kLeftOutCell, .value = 0xFF}}),
                  "$7E:2F50 read after the comparison, FF, not compared\n");
    });
}

// The first fight's start, both gates whole: every cell the battle program writes up to the loop's
// first frame wait is compared or listed, and every listed cell the battle's end reads after it was
// written since.
TEST_F(BattleStartOracleTest, TheFirstFightsStartLeavesNoCellUncompared) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        const trace::StartScenario scenario = oracle.readStartScenario();
        const StartResult result = oracle.battleStartAsItStands();
        ASSERT_TRUE(result.reachedTheLoop);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(oracle.uncoveredWrites(port, scenario.cells, result.battleWrites), "");
        EXPECT_EQ(oracle.carriedReads(port, scenario.cells, result.readsAfterThePoint), "");
    });
}

// A formation whose scripted setup keeps monster slot 0 out of reach (its validMonsterTargets $FE),
// started from the first fight's scenario: the opening lays the setup's byte into $2F46 and the port
// starts with that monster out of the AI's reach.
TEST_F(BattleStartOracleTest, AScriptedSetupKeepsItsMonstersOutOfReach) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::optional<FormationId> chosen;
    for (int id = 0; id < kFormationCount && !chosen; ++id) {
        const FormationAux& aux = getFormationAux(static_cast<FormationId>(id));
        if (aux.characterAiEnabled() &&
            charAiSetup(static_cast<CharAiId>(aux.characterAiIndex())).validMonsterTargets.bits == kSlotZeroOutOfReach) {
            chosen = static_cast<FormationId>(id);
        }
    }
    ASSERT_TRUE(chosen) << "no scripted setup keeps a monster out of reach";
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        trace::StartScenario scenario = oracle.readStartScenario();
        scenario.start.formation = FormationRef::of(*chosen);
        // Each character the setup seats has a record, taken from one outside the party played.
        game::Party& party = scenario.cells.party;
        for (const CharAiSlot& cast : charAiSetup(static_cast<CharAiId>(getFormationAux(*chosen).characterAiIndex())).slots) {
            if (cast.isEmpty()) {
                continue;
            }
            const CharacterPropId actor = cast.character.characterProperties();
            if (std::any_of(party.characters.begin(), party.characters.end(),
                            [&](const game::CharacterRecord& record) { return record.actor == actor; })) {
                continue;
            }
            for (std::size_t record = game::kCharacterRecords; record-- > 0;) {
                if (party.membership[record].party != party.currentParty) {
                    party.characters[record].actor = actor;
                    break;
                }
            }
        }
        const StartResult result = oracle.battleStart(scenario);
        ASSERT_TRUE(result.reachedTheLoop);
        ASSERT_EQ(result.cells[kMonsterReachCell], kSlotZeroOutOfReach);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        EXPECT_EQ(static_cast<std::uint8_t>(~port.aiUntargetable.monsters.bits), result.cells[kMonsterReachCell])
            << "formation " << static_cast<int>(*chosen);
        EXPECT_EQ(oracle.compareBattleState(port, scenario.cells, result.cells), "");
    });
}

// The same formation started from the first fight's party as it stands, so a character the setup
// seats has no record: the cartridge loads what it loads for them from bank $7F, the port loads
// nothing, and the comparison leaves out everything loaded through their record and holds the rest.
TEST_F(BattleStartOracleTest, ASeatedCharacterWithNoRecordIsLeftOutOfTheComparison) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::optional<FormationId> chosen;
    for (int id = 0; id < kFormationCount && !chosen; ++id) {
        const FormationAux& aux = getFormationAux(static_cast<FormationId>(id));
        if (aux.characterAiEnabled() &&
            charAiSetup(static_cast<CharAiId>(aux.characterAiIndex())).validMonsterTargets.bits == kSlotZeroOutOfReach) {
            chosen = static_cast<FormationId>(id);
        }
    }
    ASSERT_TRUE(chosen) << "no scripted setup keeps a monster out of reach";
    const std::vector<std::uint8_t>& state = firstFight();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useMonsterSprites(content().monsterSprites);
        ASSERT_TRUE(oracle.machine().restore(state));
        trace::StartScenario scenario = oracle.readStartScenario();
        scenario.start.formation = FormationRef::of(*chosen);
        const StartResult result = oracle.battleStart(scenario);
        ASSERT_TRUE(result.reachedTheLoop);
        const battle::BattleState port = trace::portStart(scenario, content().text, content().monsterSprites);
        const bool seatedWithNoRecord = std::any_of(port.battlers.begin(), port.battlers.end(),
                                                    [](const battle::Battler& b) { return b.actor && !b.record; });
        ASSERT_TRUE(seatedWithNoRecord) << "the setup seats no character without a record";
        EXPECT_EQ(oracle.compareBattleState(port, scenario.cells, result.cells), "");
    });
}

// --- the start swept -----------------------------------------------------------------------------

std::uint8_t at(const trace::StartResult& result, std::uint32_t cell) { return result.cells[cell - kWorkRam]; }

std::uint16_t wordAt(const trace::StartResult& result, std::uint32_t cell) {
    return static_cast<std::uint16_t>(at(result, cell) | at(result, cell + 1) << 8);
}

// The first formation of the field's for which `keep` holds and which no scripted setup changes.
FormationId formationWhere(bool (*keep)(const FormationAux&)) {
    for (int id = 0; id < kFormationCount; ++id) {
        const FormationAux& aux = getFormationAux(static_cast<FormationId>(id));
        if (!aux.characterAiEnabled() && keep(aux)) {
            return static_cast<FormationId>(id);
        }
    }
    throw std::runtime_error("no formation holds what the case asks for");
}

// The opening message the cartridge's start left at the head of the interrupting actions, if any.
std::optional<std::uint8_t> openingMessage(const trace::StartResult& result) {
    const std::uint8_t slot = at(result, cells::kFirstImmediateAction);
    if (slot == kNoSlot || at(result, cells::kActionCommand + 2u * slot) != kBattleMessageCommand) {
        return std::nullopt;
    }
    return at(result, cells::kActionCommand + 2u * slot + 1);
}

// The first fight in Narshe: a formation that allows a front attack only, so ChooseBattleType
// (:7556-7578) has nothing else to choose, and a play clock that draws no preemptive strike, so the
// opening shows no message (:6138-6161).
TEST_F(BattleStartOracleTest, AnOrdinaryBattleOpensAsAFrontAttackWithNoMessage) {
    OSTINATO_SKIP_WHEN_BRIEF();
    ASSERT_EQ(getFormationAux(FormationId::GUARD_X2).possibleBattleTypes(), kOnlyAFrontAttack);
    const trace::StartCheck check = checked(firstFightScenario());
    EXPECT_EQ(check.differences, "");
    EXPECT_EQ(at(check.cartridge, cells::kBattleType), kFrontAttack);
    EXPECT_EQ(check.port.battleType, battle::BattleType::Front);
    EXPECT_EQ(at(check.cartridge, cells::kBattleFlags0) & cells::kPreemptiveBit, 0);
    EXPECT_FALSE(check.port.preemptive);
    EXPECT_FALSE(openingMessage(check.cartridge));
    EXPECT_TRUE(check.port.immediateActions.empty());
}

// A front attack whose draw falls below 32 (InitBattleType_00 :7621-7641) is a preemptive strike: $B0
// bit 6 (:7639-7640) and battle message 34 ahead of everything (:6154-6161).
TEST_F(BattleStartOracleTest, APreemptiveStrikeOpensWithItsMessage) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    const std::optional<std::uint8_t> frames =
        framesWhere(scenario, [](const battle::BattleState& s) { return s.preemptive; });
    ASSERT_TRUE(frames) << "no play clock frames give the first fight a preemptive strike";
    scenario.start.playTimeFrames = *frames;
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    EXPECT_NE(at(check.cartridge, cells::kBattleFlags0) & cells::kPreemptiveBit, 0);
    EXPECT_EQ(openingMessage(check.cartridge), kPreemptiveMessage);
    ASSERT_FALSE(check.port.immediateActions.empty());
    const auto* message = std::get_if<battle::BattleMessageAction>(&check.port.immediateActions.front());
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->message, kPreemptiveMessage);
}

// A back attack (InitBattleType_01 :7657-7666) swaps every party member's row and is a back attack for
// the battle ($B1 bit 5); its opening message is 35.
TEST_F(BattleStartOracleTest, ABackAttackSwapsThePartysRows) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    scenario.start.formation =
        FormationRef::of(formationWhere([](const FormationAux& aux) { return aux.frontPossible() && aux.backPossible(); }));
    const std::optional<std::uint8_t> frames =
        framesWhere(scenario, [](const battle::BattleState& s) { return s.battleType == battle::BattleType::Back; });
    ASSERT_TRUE(frames);
    scenario.start.playTimeFrames = *frames;
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    EXPECT_EQ(at(check.cartridge, cells::kBattleType), kBackAttack);
    EXPECT_NE(at(check.cartridge, cells::kRunningCell) & kBackAttackBit, 0);
    EXPECT_EQ(openingMessage(check.cartridge), kBackAttackMessage);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::Battler& member = check.port.at(battle::BattlerSlot::character(position));
        if (!member.record) {
            continue;
        }
        const bool backRow = scenario.cells.party.membership[*member.record].backRow;
        EXPECT_EQ((at(check.cartridge, cells::kStance + 2u * position) & cells::kBackRowBit) != 0, !backRow) << position;
        EXPECT_EQ(member.backRow, !backRow) << position;
    }
}

// A pincer (InitBattleType_02 :7646-7653) puts every party member in the front row and is a back
// attack for the battle; its opening message is 37, and the opening lays the monsters on each side.
TEST_F(BattleStartOracleTest, APincerPutsThePartyInTheFrontRow) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    scenario.start.formation =
        FormationRef::of(formationWhere([](const FormationAux& aux) { return aux.frontPossible() && aux.pincerPossible(); }));
    const std::optional<std::uint8_t> frames =
        framesWhere(scenario, [](const battle::BattleState& s) { return s.battleType == battle::BattleType::Pincer; });
    ASSERT_TRUE(frames);
    scenario.start.playTimeFrames = *frames;
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    EXPECT_EQ(at(check.cartridge, cells::kBattleType), kPincerAttack);
    EXPECT_NE(at(check.cartridge, cells::kRunningCell) & kBackAttackBit, 0);
    EXPECT_EQ(openingMessage(check.cartridge), kPincerMessage);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (check.port.at(battle::BattlerSlot::character(position)).record) {
            EXPECT_EQ(at(check.cartridge, cells::kStance + 2u * position) & cells::kBackRowBit, 0) << position;
        }
    }
    EXPECT_NE(at(check.cartridge, cells::kPincerFirstSide), 0);
    EXPECT_NE(at(check.cartridge, cells::kPincerSecondSide), 0);
}

// A colosseum match (InitParty :7748-7756): the fighter stands alone in the first slot, and every
// monster slot whose monster number is below 256 holds the opponent (InitMonsters :7697).
TEST_F(BattleStartOracleTest, AColosseumMatchSeatsTheFighterAloneAgainstTheOpponent) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    const game::Party& party = scenario.cells.party;
    std::optional<CharacterPropId> fighter;
    for (std::size_t r = 0; r < game::kCharacterRecords && !fighter; ++r) {
        if (party.membership[r].party == party.currentParty) {
            fighter = party.characters[r].actor;
        }
    }
    ASSERT_TRUE(fighter);
    constexpr MonsterId opponent = MonsterId::GUARD;
    scenario.start.formation = FormationRef::of(kColosseumFormation);
    scenario.start.colosseum =
        battle::ColosseumMatch{.wager = ItemId::POTION, .prize = ItemId::ELIXIR, .opponent = opponent, .fighter = *fighter};
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    EXPECT_TRUE(check.port.colosseum);
    EXPECT_EQ(check.port.at(battle::BattlerSlot::character(0)).actor, *fighter);
    EXPECT_NE(wordAt(check.cartridge, cells::kRecordOffset), kNoRecordOffset);
    for (int position = 1; position < battle::kPartySlots; ++position) {
        EXPECT_EQ(wordAt(check.cartridge, cells::kRecordOffset + 2u * position), kNoRecordOffset) << position;
        EXPECT_FALSE(check.port.at(battle::BattlerSlot::character(position)).actor) << position;
    }
    const Formation& formation = getFormation(kColosseumFormation);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        if ((formation.monsterIdHigh & (1u << position)) == 0) {
            const std::uint32_t index = cells::kMonsterIndex + 2u * static_cast<std::uint32_t>(battle::kPartySlots + position);
            EXPECT_EQ(wordAt(check.cartridge, index), static_cast<std::uint16_t>(opponent)) << position;
            EXPECT_EQ(check.port.at(battle::BattlerSlot::monster(position)).monster, opponent) << position;
        }
    }
}

// The final battle's first stage (InitParty :7721-7747): each party slot takes the next character in
// line, and each place taken holds $FF.
TEST_F(BattleStartOracleTest, TheFinalBattlesFirstStageSeatsTheFirstInLine) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    const game::Party& party = scenario.cells.party;
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (party.membership[r].party == party.currentParty) {
            scenario.start.finalBattleOrder.push_back(r);
        }
    }
    ASSERT_FALSE(scenario.start.finalBattleOrder.empty());
    scenario.start.formation = FormationRef::of(kFinalBattleFormations[0]);
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    EXPECT_TRUE(check.port.finalBattleChain);
    for (std::size_t k = 0; k < scenario.start.finalBattleOrder.size(); ++k) {
        const std::size_t record = scenario.start.finalBattleOrder[k];
        EXPECT_EQ(wordAt(check.cartridge, cells::kRecordOffset + 2u * static_cast<std::uint32_t>(k)), record * kRecordBytes)
            << k;
        EXPECT_EQ(check.port.at(battle::BattlerSlot::character(static_cast<int>(k))).record, record) << k;
        EXPECT_EQ(at(check.cartridge, cells::kFinalBattleLine + static_cast<std::uint32_t>(k)), cells::kEndOfLine) << k;
    }
    EXPECT_TRUE(check.port.finalBattleReserve.empty());
}

// A formation asked for with its randomize flag becomes itself plus the low two bits of the first draw
// after the seed (LoadBattleProp :7956-7965); the seed is four times the play clock's frames (:6088-6090).
TEST_F(BattleStartOracleTest, ARandomizedFormationIsOneOfFour) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    constexpr FormationId asked = FormationId::GUARD_X2;
    scenario.start.formation = FormationRef::of(asked, true);
    const std::optional<std::uint8_t> frames =
        framesWhere(scenario, [](const battle::BattleState& s) { return s.formation != asked; });
    ASSERT_TRUE(frames);
    scenario.start.playTimeFrames = *frames;
    const int added = rngByte(static_cast<std::uint8_t>(*frames * 4 + 1)) & 3;
    ASSERT_NE(added, 0);
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    const auto chosen = static_cast<FormationId>(static_cast<int>(asked) + added);
    EXPECT_EQ(check.port.formation, chosen);
    EXPECT_EQ(wordAt(check.cartridge, cells::kFormationCell), static_cast<std::uint16_t>(chosen));
}

// A party with a slot left empty (InitParty :7848-7850 skips it): the slot's record offset keeps the
// fill's $FFFF and the slot is not on the field.
TEST_F(BattleStartOracleTest, AnEmptyPartySlotIsLeftOffTheField) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::StartScenario scenario = firstFightScenario();
    game::Party& party = scenario.cells.party;
    std::optional<std::uint8_t> emptied;
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (party.membership[r].party == party.currentParty && party.membership[r].slot != 0) {
            emptied = party.membership[r].slot;
            party.membership[r].party = 0;
            break;
        }
    }
    ASSERT_TRUE(emptied);
    const trace::StartCheck check = checked(scenario);
    EXPECT_EQ(check.differences, "");
    EXPECT_EQ(wordAt(check.cartridge, cells::kRecordOffset + 2u * *emptied), kNoRecordOffset);
    EXPECT_EQ(at(check.cartridge, cells::kOnTheField + 2u * *emptied) & 1, 0);
    EXPECT_FALSE(check.port.at(battle::BattlerSlot::character(*emptied)).present);
}

// Every formation once, from the first fight's party: the colosseum's with a match for its first
// member, the final battle's first stage with its members in line, and every scripted setup's cast given
// a record. Each start is the port's.
TEST_F(BattleStartOracleTest, EveryFormationStartsAsThePortDoes) {
    OSTINATO_SKIP_WHEN_BRIEF();
    const trace::StartScenario first = firstFightScenario();
    const game::Party& party = first.cells.party;
    std::vector<std::size_t> members;
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (party.membership[r].party == party.currentParty) {
            members.push_back(r);
        }
    }
    ASSERT_FALSE(members.empty());
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        for (int id = 0; id < kFormationCount; ++id) {
            const auto formation = static_cast<FormationId>(id);
            trace::StartScenario scenario = first;
            scenario.start.formation = FormationRef::of(formation);
            if (id >= static_cast<int>(FormationId::UNUSED_574)) {
                scenario.start.colosseum = battle::ColosseumMatch{.wager = ItemId::POTION,
                                                                  .prize = ItemId::ELIXIR,
                                                                  .opponent = MonsterId::GUARD,
                                                                  .fighter = party.characters[members.front()].actor};
            }
            if (formation == kFinalBattleFormations[0]) {
                scenario.start.finalBattleOrder = members;
            }
            trace::giveTheCastRecords(scenario);
            EXPECT_EQ(oracle.checkStart(scenario).differences, "") << "formation " << id;
        }
    });
}

// Drawn starts (trace::drawStartScenario()), each the port's, and between them every row of the reach
// table taken: each battle type, a preemptive strike, the colosseum, the final battle, a scripted setup,
// Gau's return, an empty slot, a morphed member, a randomized formation, a conditional battle, and each
// branch of the battle type's choice a Back Guard, a Gale Hairpin and a party of fewer than three take.
TEST_F(BattleStartOracleTest, DrawnStartsAreThePortsAndReachEveryRow) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::map<std::string_view, int> reached;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        ready(oracle);
        std::mt19937 rng{trace::kSweepSeed};
        for (int draw = 0; draw < kDrawnStarts; ++draw) {
            const trace::StartScenario scenario = trace::drawStartScenario(rng, content().text, content().monsterSprites);
            const trace::StartCheck check = oracle.checkStart(scenario);
            EXPECT_EQ(check.differences, "") << "draw " << draw << ": " << trace::describe(scenario);
            if (check.cartridge.reachedTheLoop) {
                for (const std::string_view row : trace::startReached(scenario, check.port, check.cartridge.cells)) {
                    ++reached[row];
                }
            }
        }
    });
    for (const std::string_view row : trace::startReachRows()) {
        EXPECT_GT(reached[row], 0) << row;
    }
}

}  // namespace
}  // namespace ostinato::test
