#include "damage_oracle.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/isa.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"
#include "retropp/timing.h"

#include "battle/attacker_effects.h"
#include "battle/battle_hooks.h"
#include "battle/battle_script.h"
#include "battle/damage.h"
#include "battle/status.h"
#include "data/attack_properties.h"
#include "ostinato/attack_id.h"
#include "ostinato/item_id.h"
#include "ostinato/attack_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"

namespace ostinato::trace {

namespace {

// --- the routines (battle/battle_main.asm; the battle program sits at $C2:0000) -----------------

constexpr std::uint32_t kCalcMagicDmg = 0xC22B69;
constexpr std::uint32_t kCalcDmg = 0xC22B9D;
constexpr std::uint32_t kRelicDmgEffect = 0xC20D4A;
constexpr std::uint32_t kApplyDmgMult = 0xC2370B;
constexpr std::uint32_t kCalcTargetDmg = 0xC20B83;
constexpr std::uint32_t kApplyAndShowDmg = 0xC262EF;
constexpr std::uint32_t kCalcReflectDmg = 0xC23483;
constexpr std::uint32_t kUpdateStatus = 0xC24391;
constexpr std::uint32_t kMagicStatusEffect = 0xC24406;
constexpr std::uint32_t kDoAttackerEffect = 0xC23E7D;

// `Reset` (field/reset.asm), where the reset vector's `jml` lands after `clc; xce`
// (field/header.asm): the first instruction the game runs in native mode.
constexpr std::uint32_t kReset = 0xC00019;

// The machine is advanced a microsecond at a time until the reset escape fires, so it is parked
// within an instruction or two of it; the whole start takes a few microseconds, and this many steps
// is far more than enough before the start is called a failure.
constexpr std::chrono::nanoseconds kStartStep{1'000};
constexpr int kStartStepsAllowed = 100'000;

constexpr const char* kResetKey = "reset";

struct RoutineOpening {
    const char* name;
    std::uint32_t at;
    std::array<std::uint8_t, 4> bytes;
    std::size_t length;
};
// Each routine's first instruction, as the cartridge holds it.
constexpr std::array<RoutineOpening, 10> kOpenings{{
    {"CalcMagicDmg", kCalcMagicDmg, {0xAD, 0xAF, 0x11, 0x00}, 3},    // lda $11af
    {"CalcDmg", kCalcDmg, {0xAD, 0xA2, 0x11, 0x00}, 3},              // lda $11a2
    {"RelicDmgEffect", kRelicDmgEffect, {0x08, 0xAD, 0xA4, 0x11}, 4},  // php; lda $11a4
    {"ApplyDmgMult", kApplyDmgMult, {0x5A, 0xA4, 0xBC, 0x00}, 3},    // phy; ldy $bc
    {"CalcTargetDmg", kCalcTargetDmg, {0x08, 0xE2, 0x20, 0xAD}, 4},  // php; sep #$20; lda $11a6
    {"the apply-and-show routine", kApplyAndShowDmg, {0xDA, 0x5A, 0x64, 0xF0}, 4},  // phx; phy; stz $f0
    {"CalcReflectDmg", kCalcReflectDmg, {0xDA, 0x48, 0x20, 0x00}, 3},                // phx; pha; jsr
    {"UpdateStatus", kUpdateStatus, {0xDA, 0x08, 0xC2, 0x20}, 4},                    // phx; php; rep #$20
    {"MagicStatusEffect", kMagicStatusEffect, {0x08, 0xC2, 0x20, 0xB9}, 4},          // php; rep #$20; lda $3ee4,y
    {"DoAttackerEffect", kDoAttackerEffect, {0xDA, 0x08, 0xE2, 0x30}, 4},            // phx; php; sep #$30
}};

// The frame the battle program calls them in: data bank $7E and direct page $0000, so every cell is
// a work-RAM address. CalcDmg and RelicDmgEffect are entered with both register widths narrow,
// ApplyDmgMult and CalcTargetDmg with the accumulator wide and the index registers narrow;
// interrupts are off throughout.
constexpr std::uint8_t kBattleDataBank = 0x7E;
constexpr std::uint16_t kBattleDirectPage = 0x0000;
constexpr std::uint8_t kByteWideStatus = 0x34;
constexpr std::uint8_t kWideAccumulatorStatus = 0x14;

// --- the cells the per-target pass reads and writes ----------------------------------------------

constexpr std::uint32_t kWorkRam = DamageOracle::kWorkRam;
constexpr std::uint32_t kEffect = kWorkRam + 0x11A9;            // the special effect, its index doubled
constexpr std::uint32_t kFirstAttackFlags = kWorkRam + 0x00B2;  // bit 7: a drain is capped
constexpr std::uint32_t kAnimationCommand = kWorkRam + 0x00B4;
constexpr std::uint32_t kCommand = kWorkRam + 0x00B5;
constexpr std::uint32_t kAttackIndex = kWorkRam + 0x00B6;
constexpr std::uint32_t kSecondary = kWorkRam + 0x00B7;
constexpr std::uint32_t kRngPosition = kWorkRam + 0x00BE;
constexpr std::uint32_t kDamageModification = kWorkRam + 0x3414;
constexpr std::uint32_t kGolemBlock = kWorkRam + 0x3A82;
constexpr std::uint32_t kInterceptorBlock = kWorkRam + 0x3A83;
constexpr std::uint32_t kForcefield = kWorkRam + 0x3EC8;
// The battle script's queue: where the attack's animation went, the next free place, the queue.
constexpr std::uint32_t kAnimationPlace = kWorkRam + 0x3A71;
constexpr std::uint32_t kQueueEnd = kWorkRam + 0x3A72;
constexpr std::uint32_t kQueue = kWorkRam + 0x2D6E;
constexpr std::uint8_t kQueueCommandSize = 4;
// Per battler, at twice the battler's index.
constexpr std::uint32_t kLevel = kWorkRam + 0x3B18;
constexpr std::uint32_t kDefenses = kWorkRam + 0x3BB8;   // defense, then magic defense
constexpr std::uint32_t kHp = kWorkRam + 0x3BF4;
constexpr std::uint32_t kMp = kWorkRam + 0x3C08;
constexpr std::uint32_t kMaxHp = kWorkRam + 0x3C1C;
constexpr std::uint32_t kMaxMp = kWorkRam + 0x3C30;
constexpr std::uint32_t kAbsorbedNullified = kWorkRam + 0x3BCC;
constexpr std::uint32_t kWeakHalved = kWorkRam + 0x3BE0;
constexpr std::uint32_t kMonsterFlags = kWorkRam + 0x3C95;
constexpr std::uint32_t kStance = kWorkRam + 0x3AA1;
constexpr std::uint32_t kStatus12 = kWorkRam + 0x3EE4;
constexpr std::uint32_t kStatus34 = kWorkRam + 0x3EF8;
constexpr std::uint32_t kToSet12 = kWorkRam + 0x3DD4;
constexpr std::uint32_t kToSet34 = kWorkRam + 0x3DE8;
constexpr std::uint32_t kToClear12 = kWorkRam + 0x3DFC;
constexpr std::uint32_t kToClear34 = kWorkRam + 0x3E10;
constexpr std::uint32_t kController = kWorkRam + 0x32B9;
constexpr std::uint32_t kLastAttacker = kWorkRam + 0x32E0;
constexpr std::uint32_t kTaken = kWorkRam + 0x33D0;
constexpr std::uint32_t kHealed = kWorkRam + 0x33E4;

// --- the cells taking and showing the damage reads and writes ------------------------------------

// Per battler, a word at twice the battler's index: its bit in the battler sets below (the party
// members' in the low byte, the monsters' in the high byte), what is to be worked out again for it,
// and the battler that last damaged it.
constexpr std::uint32_t kBattlerBit = kWorkRam + 0x3018;
constexpr std::uint32_t kToUpdate = kWorkRam + 0x3204;
constexpr std::uint32_t kLastDamagedBy = kWorkRam + 0x327C;
constexpr std::uint32_t kMissed = kWorkRam + 0x3A5A;
constexpr std::uint32_t kInvincible = kWorkRam + 0x3A3C;
constexpr std::uint32_t kGolemHp = kWorkRam + 0x3A36;
constexpr std::uint32_t kWeaponSpellcast = kWorkRam + 0x3A89;
constexpr std::uint32_t kBlackBeltTargets = kWorkRam + 0x3419;  // a party member's bit clear: it may be countered for
// The numerals for five or more battlers are copied to the graphics buffer, 20 bytes per command,
// counted from the buffer's start.
constexpr std::uint32_t kNumeralBuffer = kWorkRam + 0x2BCE;
constexpr std::uint32_t kNumeralBufferCount = kWorkRam + 0x3A34;
constexpr std::uint32_t kNumeralBufferSize = 20;
// The castable spells to be worked out again.
constexpr std::uint8_t kEnabledMagicBit = 0x80;
// The numeral commands: one battler's numeral, or every battler's at once from the buffer.
constexpr std::uint8_t kOneNumeral = 0x0B;
constexpr std::uint8_t kEveryNumeral = 0x03;
// A numeral's word: the gain mark, the miss mark and the amount.
constexpr std::uint16_t kHealMark = 0x8000;
constexpr std::uint16_t kMissMark = 0x4000;
constexpr std::uint16_t kAmountBits = 0x3FFF;
constexpr std::uint8_t kPartyBits = 0x0F;

// --- the cells the reflected attack's aim and hit check read ------------------------------------

constexpr std::uint32_t kTargets = kWorkRam + 0x00B8;
constexpr std::uint32_t kAimingRules = kWorkRam + 0x00BA;
constexpr std::uint32_t kAimFlags = kWorkRam + 0x3A46;          // bit 1: the next aim is exact
constexpr std::uint32_t kEarlierTargets = kWorkRam + 0x3A4E;
constexpr std::uint32_t kEnemyCharacters = kWorkRam + 0x3A40;   // party members fighting against the party
constexpr std::uint32_t kBattleType = kWorkRam + 0x201F;
constexpr std::uint32_t kPincerFirstSide = kWorkRam + 0x2EAC;
constexpr std::uint32_t kPincerSecondSide = kWorkRam + 0x2EAD;
// The reach check ANDs four cells: the party members' bits of the first two words with the
// monsters' bits of a byte and the next two words.
constexpr std::uint32_t kReachMonsters = kWorkRam + 0x2F46;
constexpr std::uint32_t kReachCharacters = kWorkRam + 0x3403;
constexpr std::uint32_t kReachFirst = kWorkRam + 0x3A78;
constexpr std::uint32_t kReachSecond = kWorkRam + 0x3408;
constexpr std::uint32_t kCountersEnabled = kWorkRam + 0x341A;   // negative: counterattacks are on
// The reach check loads the counterattack cell as a word, so its sign is this next cell's: negative,
// battlers away from the field are dropped. A command starts it at $FF and only an attack aimed at
// its own attacker clears it.
constexpr std::uint32_t kReachKeepsAway = kWorkRam + 0x341B;
constexpr std::uint32_t kAwayFromTheField = kWorkRam + 0x3F2C;
constexpr std::uint32_t kTargetCount = kWorkRam + 0x3EC9;
constexpr std::uint32_t kQuietMisses = kWorkRam + 0x341C;       // 0: misses show nothing
constexpr std::uint32_t kHitInTheBack = kWorkRam + 0x3A54;
constexpr std::uint32_t kQueuedCommand = kWorkRam + 0x3A7C;
constexpr std::uint32_t kLastSpell = kWorkRam + 0x3410;
constexpr std::uint32_t kLastItem = kWorkRam + 0x3411;
constexpr std::uint32_t kExtraHits = kWorkRam + 0x3405;         // one less than the hits still to come
// The params blocks the script's animations read, sixteen bytes each, and where the next goes; the
// battle-type step reads the attacker of the block before it.
constexpr std::uint32_t kParamsBlocks = kWorkRam + 0x2C6E;
constexpr std::uint32_t kNextParamsBlock = kWorkRam + 0x3A32;
constexpr std::uint8_t kParamsBlockSize = 16;
// Per battler, at twice the battler's index.
constexpr std::uint32_t kOnTheField = kWorkRam + 0x3AA0;        // bit 0
constexpr std::uint32_t kCharmedBy = kWorkRam + 0x3395;
constexpr std::uint32_t kStamina = kWorkRam + 0x3B40;
constexpr std::uint32_t kEvadeAndMagicBlock = kWorkRam + 0x3B54;  // evade, then magic block
constexpr std::uint32_t kBlockGraphics = kWorkRam + 0x3CE4;       // a blow's, then a spell's
constexpr std::uint32_t kRetort = kWorkRam + 0x3E4C;              // bit 0
constexpr std::uint32_t kLastHitCommandBy = kWorkRam + 0x3290;
constexpr std::uint32_t kLastHitCommand = kWorkRam + 0x3D48;
constexpr std::uint32_t kLastHitSpellBy = kWorkRam + 0x3291;
constexpr std::uint32_t kLastHitSpell = kWorkRam + 0x3D49;
constexpr std::uint32_t kLastHitItemBy = kWorkRam + 0x32A4;
constexpr std::uint32_t kLastHitItem = kWorkRam + 0x3D5C;
constexpr std::uint32_t kLastHitElementBy = kWorkRam + 0x32A5;
constexpr std::uint32_t kLastHitElement = kWorkRam + 0x3D5D;

constexpr std::uint8_t kAimExactlyBit = 0x02;
constexpr std::uint8_t kCannotBeCriticalBit = 0x02;
constexpr std::uint8_t kVanishAppliesBit = 0x80;
constexpr std::uint8_t kInstantDeathProtectedBit = 0x04;
constexpr std::uint8_t kEquipmentBlocks = 0x0F;
constexpr std::uint8_t kCountersOn = 0x80;
constexpr std::uint8_t kNotQuiet = 0xFF;
constexpr std::uint8_t kAwayUnreachable = 0xFF;  // kReachKeepsAway as a command starts
constexpr std::uint8_t kNoHitsToCome = 0xFF;
// The bounce's two commands.
constexpr std::uint8_t kReflectedCommand = 0x09;
constexpr std::uint8_t kScatteredCommand = 0x15;
// The bounce's block: a battler per slot, then a block per party slot, then a battler set.
constexpr std::size_t kBlockBlocks = 10;
constexpr std::size_t kBlockSet = 14;

// Damage modification on and off; a link or block that names no one; a ledger entry with nothing in it.
constexpr std::uint8_t kOn = 0xFF;
constexpr std::uint8_t kNoOne = 0xFF;
constexpr std::uint16_t kNoDamage = 0xFFFF;
constexpr std::uint8_t kDrainCappedBit = 0x80;
constexpr std::uint8_t kWaitingBit = 0x80;
// The monster flags byte: undead, the Imp critical, human, the name hidden, dying at 0 MP.
constexpr std::uint8_t kUndeadBit = 0x80;
constexpr std::uint8_t kImpCriticalBit = 0x40;
constexpr std::uint8_t kHumanBit = 0x10;
constexpr std::uint8_t kNameHiddenBit = 0x04;
constexpr std::uint8_t kDiesAtZeroMpBit = 0x01;
// The stance byte: Defending, the back row.
constexpr std::uint8_t kDefendingBit = 0x02;
constexpr std::uint8_t kBackRowBit = 0x20;
// The queue's place for the attack's animation is filled with this before a call, and the port's
// with an animation of this second value, so a rewrite on either side shows.
constexpr std::uint8_t kUnwritten = 0xEE;

// The second attack-flags byte as a command starts it, every bit set, with the two bits the damage
// routines read: set while the hand is not held in both hands, set while an attack that ignores
// defense is not multiplied (docs/contracts/battle_commands.md, "A command's start").
constexpr std::uint8_t kAttackFlagsAtStart = 0xFF;
constexpr std::uint8_t kNotTwoHanded = 0x40;
constexpr std::uint8_t kIgnoreDefenseStopsMultiplier = 0x10;

// The first relic word holds relic effects 1 in its low byte, with that byte's top bit taken by
// relic effects 2's top bit, and relic effects 3 in its high byte (InitCharBattle).
constexpr std::uint8_t kLowSevenBits = 0x7F;
constexpr std::uint8_t kTopBit = 0x80;

std::uint16_t slotIndex(battle::BattlerSlot slot) {
    return static_cast<std::uint16_t>(slot.index() * 2);
}

// A battler set as the battle program holds it: a word with the party members' bits in its low byte
// and the monsters' in its high byte.
std::uint16_t maskOf(battle::BattlerSet set) {
    return static_cast<std::uint16_t>(set.characters.bits | set.monsters.bits << 8);
}

battle::BattlerSet setOf(std::uint16_t mask) {
    return battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(mask & 0xFF)),
                              BattleSlotMask(static_cast<std::uint8_t>(mask >> 8))};
}

std::uint8_t byte(std::mt19937& rng) {
    return static_cast<std::uint8_t>(rng() & 0xFF);
}

}  // namespace

DamageOracle::DamageOracle(std::span<const std::uint8_t> image) {
    machine_.hostRom(image);
    for (const RoutineOpening& routine : kOpenings) {
        const std::vector<std::uint8_t> bytes =
            machine_.read(retropp::MemoryRegion{.at = routine.at, .size = static_cast<std::uint32_t>(routine.length)});
        if (!std::equal(bytes.begin(), bytes.end(), routine.bytes.begin())) {
            throw std::runtime_error(std::string{"the cartridge does not hold "} + routine.name +
                                     " where the US cartridges do");
        }
    }

    machine_.registerEscapes(retropp::escapes(retropp::GuestEscape{
        .key = kResetKey, .at = kReset, .handler = [this](retropp::Vm&, std::uint32_t) { reset_ = true; }}));
    // The trace's watches cover the damage word and the multiplier's steps; their writes during a
    // call are what writes() hands back.
    trace_ = std::make_unique<DamageTrace>(
        machine_,
        [this](const std::string& line) {
            if (tracing_) {
                traced_.push_back(line);
            }
        },
        [] { return std::uint64_t{0}; },
        [this](std::uint32_t at, std::uint8_t value, bool write) {
            if (recording_ && write && (at == kDamage || at == kDamage + 1 || at == kMultiplier)) {
                writes_.push_back(WatchedWrite{.at = at, .value = value});
            }
        });

    const retropp::RoutineBinding byteWide{
        .inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
        .isa = retropp::Isa::Wdc65816};
    calcMagicDmg_ = machine_.bindRoutine<Call>(kCalcMagicDmg, byteWide);
    calcDmg_ = machine_.bindRoutine<Call>(kCalcDmg, byteWide);
    relicDmgEffect_ = machine_.bindRoutine<Call>(kRelicDmgEffect, byteWide);
    applyDmgMult_ = machine_.bindRoutine<Multiply>(
        kApplyDmgMult, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                        .output = retropp::snes::C,
                        .isa = retropp::Isa::Wdc65816});
    applyAndShowDmg_ = machine_.bindRoutine<Call>(
        kApplyAndShowDmg, {.inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                           .isa = retropp::Isa::Wdc65816});
    calcReflectDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcReflectDmg,
        {.inputs = {retropp::snes::C, retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    calcTargetDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcTargetDmg,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    updateStatus_ = machine_.bindRoutine<PassCall>(
        kUpdateStatus,
        {.inputs = {retropp::snes::P, retropp::snes::DB, retropp::snes::D}, .isa = retropp::Isa::Wdc65816});
    magicStatusEffect_ = machine_.bindRoutine<TargetCall>(
        kMagicStatusEffect,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    doAttackerEffect_ = machine_.bindRoutine<Call>(kDoAttackerEffect, byteWide);
}

DamageOracle::~DamageOracle() = default;

void DamageOracle::start() {
    // The machine runs on the game's own tick, a microsecond at a time, until the reset escape
    // fires, and is parked there.
    machine_.run(retropp::Vm::Advance::OnTick);
    for (int step = 0; step < kStartStepsAllowed && !reset_; ++step) {
        machine_.advanceTick(kStartStep);
    }
    machine_.stop();
    if (!reset_) {
        throw std::runtime_error("the cartridge did not reach its reset code");
    }
}

void DamageOracle::call(const std::function<void()>& routine) {
    if (!reset_) {
        throw std::logic_error("the oracle's cartridge has not been started (DamageOracle::start)");
    }
    writes_.clear();
    trace_->forgetContextWrites();
    recording_ = true;
    routine();
    recording_ = false;
}

void runOnLoop(const std::function<void()>& work) {
    TickClock clock{retropp::TimingProfile::Snes.tickPeriod()};
    retropp::RunLoop loop{clock, retropp::TimingProfile::Snes};
    std::exception_ptr failure;
    bool done = false;
    loop.simTick([&](const retropp::InputState&) {
        if (!done) {
            done = true;
            try {
                work();
            } catch (...) {
                failure = std::current_exception();
            }
            loop.exitRequest();
        }
    });
    loop.renderLoop([] {});
    loop.exitAction([] { return retropp::ExitVerdict::Proceed; });
    loop.run();
    if (failure) {
        std::rethrow_exception(failure);
    }
}

std::uint16_t DamageOracle::magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level) {
    writeByte(kPower, power);
    writeByte(kAttackerStat, magicStat);
    writeByte(kAttackerLevel, level);
    writeWord(kDamage, 0);
    call([&] { calcMagicDmg_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    return readWord(kDamage);
}

std::uint16_t DamageOracle::calcDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeContext(scenario);
    call([&] { calcDmg_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[0]);
    return readWord(kDamage);
}

std::uint16_t DamageOracle::relicBonus(const DamageScenario& scenario, std::uint16_t damage) {
    writeScenario(scenario);
    writeWord(kDamage, damage);
    call([&] {
        relicDmgEffect_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readWord(kDamage);
}

MultiplierResult DamageOracle::applyMultiplier(const DamageScenario& scenario) {
    writeScenario(scenario);
    std::uint16_t damage = 0;
    call([&] {
        damage = applyDmgMult_(static_cast<std::uint16_t>(scenario.attack.damage), kWideAccumulatorStatus,
                               kBattleDataBank, kBattleDirectPage);
    });
    return MultiplierResult{.damage = damage, .stepsLeft = readByte(kMultiplier)};
}

TargetDamageResult DamageOracle::targetDamage(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeContext(scenario);
    call([&] {
        calcTargetDmg_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                       kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[1]);
    return readTargetResult();
}

ApplyDamageResult DamageOracle::applyDamage(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeContext(scenario);
    call([&] {
        applyAndShowDmg_(slotIndex(scenario.attacker), kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[2]);
    return readApplyResult();
}

void DamageOracle::writeApplyCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    writeWord(kMissed, maskOf(attack.scratch.missed));
    writeWord(kInvincible, maskOf(state.invincible));
    writeWord(kGolemHp, state.golemHp);
    writeByte(kWeaponSpellcast, attack.scratch.weaponSpellcast.raw);
    writeByte(kBlackBeltTargets, static_cast<std::uint8_t>(~state.scratch.blackBeltEligible.characters.bits));
    writeByte(kQueueEnd, 0);
    writeByte(kNumeralBufferCount, 0);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeWord(kToUpdate + at, battler.pendingUpdates.enabledMagic ? kEnabledMagicBit : 0);
        writeByte(kLastDamagedBy + at,
                  battler.lastDamagedBy ? static_cast<std::uint8_t>(slotIndex(*battler.lastDamagedBy)) : kNoOne);
    }
}

ApplyDamageResult DamageOracle::readApplyResult(std::size_t firstNumeralCommand, std::uint32_t firstNumeralBuffer) {
    ApplyDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.hp[i] = readWord(kHp + at);
        result.mp[i] = readWord(kMp + at);
        result.statusesToSet[i] = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1),
                                             readByte(kToSet34 + at), readByte(kToSet34 + at + 1)}};
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
        const std::uint8_t damager = readByte(kLastDamagedBy + at);
        if (damager != kNoOne) {
            result.lastDamagedBy[i] = battle::BattlerSlot::fromIndex(damager / 2);
        }
        result.enabledMagic[i] = (readByte(kToUpdate + at) & kEnabledMagicBit) != 0;
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
    }
    result.blackBeltEligible.characters =
        BattleSlotMask(static_cast<std::uint8_t>(~readByte(kBlackBeltTargets) & kPartyBits));
    result.missed = setOf(readWord(kMissed));
    result.golemHp = readWord(kGolemHp);
    result.weaponSpellcast = readByte(kWeaponSpellcast);
    result.shown = readNumerals(firstNumeralCommand, firstNumeralBuffer);
    return result;
}

std::vector<battle::DamageNumbersShown> DamageOracle::readNumerals(std::size_t firstCommand,
                                                                   std::uint32_t firstBuffer) {
    const auto numeral = [](battle::BattlerSlot slot, std::uint16_t word) {
        return battle::DamageNumber{.target = slot,
                                    .amount = static_cast<std::uint16_t>(word & kAmountBits),
                                    .miss = (word & kMissMark) != 0,
                                    .heals = (word & kHealMark) != 0};
    };
    std::vector<battle::DamageNumbersShown> shown;
    const std::uint8_t end = readByte(kQueueEnd);
    if (end == 0) {
        return shown;
    }
    const std::vector<std::uint8_t> queue = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = end});
    std::uint32_t buffers = firstBuffer;
    for (std::size_t at = firstCommand * kQueueCommandSize; at + kQueueCommandSize <= queue.size();
         at += kQueueCommandSize) {
        const std::uint8_t id = queue[at];
        if (id == kEveryNumeral) {
            battle::DamageNumbersShown together{.wait = battle::DamageNumberWait::UntilGone};
            const std::uint32_t buffer = kNumeralBuffer + kNumeralBufferSize * buffers++;
            for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
                const std::uint16_t word = readWord(buffer + static_cast<std::uint32_t>(index * 2));
                if (word != kNoDamage) {
                    together.numbers.push_back(numeral(battle::BattlerSlot::fromIndex(index), word));
                }
            }
            shown.push_back(std::move(together));
            continue;
        }
        if (id != kOneNumeral) {
            throw std::runtime_error("the apply-and-show routine wrote a command that is not a numeral: " +
                                     std::to_string(id));
        }
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(queue[at + 1]);
        const auto word = static_cast<std::uint16_t>(queue[at + 2] | queue[at + 3] << 8);
        // One pass shows its battlers last slot first, so a slot no lower than the one before begins
        // the second pass.
        const bool continues = !shown.empty() && shown.back().wait == battle::DamageNumberWait::CarryOn &&
                               !shown.back().numbers.empty() &&
                               shown.back().numbers.back().target.index() > slot.index();
        if (!continues) {
            shown.push_back(battle::DamageNumbersShown{.wait = battle::DamageNumberWait::CarryOn});
        }
        shown.back().numbers.push_back(numeral(slot, word));
    }
    return shown;
}

namespace {

// What the port's state and script hold after the damage is taken and shown.
ApplyDamageResult portApplied(const battle::BattleState& state, const battle::AttackInFlight& attack,
                              const battle::BattleScriptBuilder& script) {
    ApplyDamageResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.hp[i] = battler.hp;
        result.mp[i] = battler.mp;
        result.statusesToSet[i] = battler.statusesToSet;
        std::uint8_t mark = battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : 0;
        mark |= battler.retaliationPending ? kWaitingBit : 0;
        result.lastAttackerMark[i] = mark;
        result.lastDamagedBy[i] = battler.lastDamagedBy;
        result.enabledMagic[i] = battler.pendingUpdates.enabledMagic;
    }
    result.blackBeltEligible = state.scratch.blackBeltEligible;
    result.missed = attack.scratch.missed;
    result.golemHp = state.golemHp;
    result.weaponSpellcast = attack.scratch.weaponSpellcast.raw;
    result.ledger = attack.scratch.ledger;
    for (const battle::BattleScriptCommand& command : script.commands()) {
        if (const auto* numerals = std::get_if<battle::DamageNumbersShown>(&command)) {
            result.shown.push_back(*numerals);
        }
    }
    return result;
}

std::string slotText(battle::BattlerSlot slot) {
    return std::to_string(slot.index());
}

std::string shownText(const battle::ReflectedAttackShown& shown) {
    std::string text = "bounced attack " + std::to_string(static_cast<int>(shown.attack)) + ":";
    for (std::size_t i = 0; i < shown.bouncedTo.size(); ++i) {
        if (shown.bouncedTo[i]) {
            text += " " + std::to_string(i) + ">" + slotText(*shown.bouncedTo[i]);
        }
    }
    text += " hit " + std::to_string(maskOf(shown.bouncesThatHit)) + " blocks";
    for (const battle::BlockShown block : shown.blocks) {
        text += " " + std::to_string(static_cast<int>(block));
    }
    return text;
}

std::string shownText(const battle::ScatteredHitsShown& shown) {
    std::string text = "scattered attack " + std::to_string(static_cast<int>(shown.attack)) + " from " +
                       slotText(shown.attacker) + ":";
    for (const battle::BattlerSlot target : shown.targets) {
        text += " " + slotText(target);
    }
    return text;
}

}  // namespace

ApplyDamageResult portApplyDamage(DamageScenario scenario) {
    battle::BattleScriptBuilder script;
    battle::applyDamage(scenario.state, scenario.attack, scenario.attacker, script);
    return portApplied(scenario.state, scenario.attack, script);
}

ReflectedAttackResult portReflectedAttack(DamageScenario scenario) {
    battle::BattleScriptBuilder script;
    battle::BattleHooks hooks;
    battle::resolveReflectedAttack(scenario.state, hooks, script, scenario.attack, scenario.attacker);
    ReflectedAttackResult result;
    result.applied = portApplied(scenario.state, scenario.attack, script);
    for (const battle::BattleScriptCommand& command : script.commands()) {
        if (const auto* bounces = std::get_if<battle::ReflectedAttackShown>(&command)) {
            result.shownAs = shownText(*bounces);
        } else if (const auto* hits = std::get_if<battle::ScatteredHitsShown>(&command)) {
            result.shownAs = shownText(*hits);
        }
    }
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        result.statusesToClear[i] = battler.statusesToClear;
        if (battler.lastHit.command) {
            result.lastHitBy[i] = battler.lastHit.command->by;
        }
    }
    result.damage = scenario.attack.damage;
    result.canBeCritical = scenario.attack.canBeCritical;
    result.extraHits = scenario.state.extraHits;
    result.golemBlocked = scenario.attack.scratch.golemBlocked;
    result.interceptorBlocked = scenario.attack.scratch.interceptorBlocked;
    result.rngPosition = scenario.state.rng.position();
    return result;
}

ReflectedAttackResult DamageOracle::reflectedAttack(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeAimCells(scenario);
    call([&] {
        calcReflectDmg_(maskOf(scenario.attack.params.reflectedOff), slotIndex(scenario.attacker),
                        kWideAccumulatorStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readReflectResult();
}

void DamageOracle::writeAimCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::AimingRules& rules = attack.rules;
    std::uint8_t aiming = 0;
    aiming |= rules.noCharacterTargets ? 0x80 : 0;
    aiming |= rules.randomTarget ? 0x40 : 0;
    aiming |= rules.fallBackToEarlierTargets ? 0x20 : 0;
    aiming |= rules.reflected ? 0x10 : 0;
    aiming |= rules.mayTargetTheFallen ? 0x08 : 0;
    aiming |= rules.keepTargetsThatAreGone ? 0x04 : 0;
    aiming |= rules.opposingSideOnly ? 0x02 : 0;
    aiming |= rules.excludeAttacker ? 0x01 : 0;
    writeByte(kAimingRules, aiming);
    writeWord(kTargets, maskOf(attack.targets));
    writeByte(kAimFlags, attack.scratch.aimExactly ? kAimExactlyBit : 0);
    writeWord(kEarlierTargets, maskOf(attack.scratch.earlierTargets));
    battle::BattlerSet enemies;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).actsAsEnemy) {
            enemies.add(battle::BattlerSlot::character(position));
        }
    }
    writeWord(kEnemyCharacters, maskOf(enemies));
    writeByte(kBattleType, static_cast<std::uint8_t>(state.battleType));
    writeByte(kPincerFirstSide, state.pincerSides[0].bits);
    writeByte(kPincerSecondSide, state.pincerSides[1].bits);

    // The reach check's four cells, written so that what they leave between them is the port's own
    // reachable set: on the field and counted in the battle, not hidden from aiming by its AI, and
    // for a party member not seized.
    battle::BattlerSet seized;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).seizedBy) {
            seized.add(battle::BattlerSlot::character(position));
        }
    }
    writeByte(kReachMonsters, 0xFF);
    writeByte(kReachCharacters, static_cast<std::uint8_t>(~seized.characters.bits));
    writeWord(kReachFirst, maskOf(state.targetableOrPresent & state.countedInBattle));
    writeWord(kReachSecond, static_cast<std::uint16_t>(~maskOf(state.aiUntargetable)));
    writeByte(kCountersEnabled, state.scratch.countersEnabled ? kCountersOn : 0);
    writeByte(kReachKeepsAway, attack.scratch.awayFromTheFieldUnreachable ? kAwayUnreachable : 0);
    writeWord(kAwayFromTheField, maskOf(state.awayFromTheField));

    writeByte(kTargetCount, static_cast<std::uint8_t>(attack.scratch.targetCount));
    writeByte(kQuietMisses, attack.scratch.quietMisses ? 0 : kNotQuiet);
    writeWord(kHitInTheBack, maskOf(attack.scratch.hitInTheBack));
    writeByte(kQueuedCommand, static_cast<std::uint8_t>(attack.queuedCommand));
    writeByte(kLastSpell, attack.scratch.lastSpell ? static_cast<std::uint8_t>(*attack.scratch.lastSpell) : kNoOne);
    writeByte(kLastItem, attack.scratch.lastItem ? static_cast<std::uint8_t>(*attack.scratch.lastItem) : kNoOne);
    writeByte(kExtraHits, state.extraHits ? static_cast<std::uint8_t>(*state.extraHits - 1) : kNoHitsToCome);
    // The block before the bounce's: the frame's own, whose attacker is the caster.
    writeByte(kNextParamsBlock, kParamsBlockSize);
    writeByte(kParamsBlocks + 1, static_cast<std::uint8_t>(scenario.attacker.index()));

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kOnTheField + at, battler.present ? 1 : 0);
        writeByte(kCharmedBy + at, battler.charmedBy ? static_cast<std::uint8_t>(slotIndex(*battler.charmedBy)) : kNoOne);
        writeByte(kStamina + at, battler.stamina);
        writeByte(kEvadeAndMagicBlock + at, battler.evadeFactor);
        writeByte(kEvadeAndMagicBlock + at + 1, battler.magicBlockFactor);
        writeByte(kBlockGraphics + at, battler.physicalBlockGraphics.bits);
        writeByte(kBlockGraphics + at + 1, battler.magicBlockGraphics.bits);
        writeByte(kRetort + at, battler.triggers.retort ? 1 : 0);
        const battle::LastHits& last = battler.lastHit;
        const auto by = [](const auto& hit) {
            return hit ? static_cast<std::uint8_t>(slotIndex(hit->by)) : kNoOne;
        };
        writeByte(kLastHitCommandBy + at, by(last.command));
        writeByte(kLastHitCommand + at, last.command ? static_cast<std::uint8_t>(last.command->with) : kNoOne);
        writeByte(kLastHitSpellBy + at, by(last.spell));
        writeByte(kLastHitSpell + at, last.spell ? static_cast<std::uint8_t>(last.spell->with) : kNoOne);
        writeByte(kLastHitItemBy + at, by(last.item));
        writeByte(kLastHitItem + at, last.item ? static_cast<std::uint8_t>(last.item->with) : kNoOne);
        writeByte(kLastHitElementBy + at, by(last.element));
        writeByte(kLastHitElement + at, last.element ? last.element->with.bits : 0);
    }
}

ReflectedAttackResult DamageOracle::readReflectResult() {
    ReflectedAttackResult result;
    result.applied = readApplyResult(1, 0);
    const auto slotByte = [](std::uint8_t value) -> std::optional<battle::BattlerSlot> {
        if (value == kNoOne) {
            return std::nullopt;
        }
        return battle::BattlerSlot::fromIndex(value / 2);
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.statusesToClear[i] = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                               readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}};
        result.lastHitBy[i] = slotByte(readByte(kLastHitCommandBy + at));
    }
    const std::vector<std::uint8_t> command = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    const std::vector<std::uint8_t> block =
        machine_.read(retropp::MemoryRegion{.at = kParamsBlocks + kParamsBlockSize, .size = kParamsBlockSize});
    const auto attack = static_cast<AttackId>(command[2]);
    const std::uint16_t set = static_cast<std::uint16_t>(block[kBlockSet] | block[kBlockSet + 1] << 8);
    if (command[0] == kReflectedCommand) {
        battle::ReflectedAttackShown shown{.attack = attack};
        for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
            if (block[i] != kNoOne) {
                shown.bouncedTo[i] = battle::BattlerSlot::fromIndex(block[i]);
            }
        }
        shown.bouncesThatHit = setOf(set);
        for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
            shown.blocks[i] = static_cast<battle::BlockShown>(block[kBlockBlocks + i]);
        }
        result.shownAs = shownText(shown);
    } else if (command[0] == kScatteredCommand) {
        battle::ScatteredHitsShown shown{.attack = attack};
        for (int i = battle::kBattlerSlots - 1; i >= 0; --i) {
            if (block[static_cast<std::size_t>(i)] != kNoOne) {
                shown.targets.push_back(battle::BattlerSlot::fromIndex(block[static_cast<std::size_t>(i)]));
            }
        }
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            if (setOf(set).has(battle::BattlerSlot::fromIndex(index))) {
                shown.attacker = battle::BattlerSlot::fromIndex(index);
            }
        }
        result.shownAs = shownText(shown);
    } else {
        result.shownAs = "command " + std::to_string(command[0]);
    }
    result.damage = readWord(kDamage);
    result.canBeCritical = (readByte(kFirstAttackFlags) & kCannotBeCriticalBit) == 0;
    const std::uint8_t hits = readByte(kExtraHits);
    if (hits != kNoHitsToCome) {
        result.extraHits = hits + 1;
    }
    result.golemBlocked = slotByte(readByte(kGolemBlock));
    result.interceptorBlocked = slotByte(readByte(kInterceptorBlock));
    result.rngPosition = readByte(kRngPosition);
    return result;
}

std::string differences(const ReflectedAttackResult& port, const ReflectedAttackResult& guest) {
    std::string out = differences(port.applied, guest.applied);
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    const auto slotName = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? std::to_string(slot->index()) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("statuses to clear" + slot, statuses(port.statusesToClear[i]), statuses(guest.statusesToClear[i]));
        note("last hit by" + slot, slotName(port.lastHitBy[i]), slotName(guest.lastHitBy[i]));
    }
    note("shown", port.shownAs, guest.shownAs);
    note("damage", std::to_string(port.damage), std::to_string(guest.damage));
    note("can be critical", std::to_string(port.canBeCritical), std::to_string(guest.canBeCritical));
    note("hits to come", port.extraHits ? std::to_string(*port.extraHits) : "none",
         guest.extraHits ? std::to_string(*guest.extraHits) : "none");
    note("Golem's block", slotName(port.golemBlocked), slotName(guest.golemBlocked));
    note("Interceptor's block", slotName(port.interceptorBlocked), slotName(guest.interceptorBlocked));
    note("random-number position", std::to_string(port.rngPosition), std::to_string(guest.rngPosition));
    return out;
}

std::string differences(const ApplyDamageResult& port, const ApplyDamageResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    const auto slotName = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? std::to_string(slot->index()) : std::string{"none"};
    };
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto numerals = [](const std::vector<battle::DamageNumbersShown>& shown) {
        std::string text;
        for (const battle::DamageNumbersShown& one : shown) {
            text += one.wait == battle::DamageNumberWait::UntilGone ? "[together" : "[one at a time";
            for (const battle::DamageNumber& number : one.numbers) {
                text += " " + std::to_string(number.target.index()) + ":" +
                        (number.miss ? std::string{"miss"} : std::to_string(number.amount)) + (number.heals ? "+" : "");
            }
            text += "]";
        }
        return text;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("HP" + slot, std::to_string(port.hp[i]), std::to_string(guest.hp[i]));
        note("MP" + slot, std::to_string(port.mp[i]), std::to_string(guest.mp[i]));
        note("statuses to set" + slot, statuses(port.statusesToSet[i]), statuses(guest.statusesToSet[i]));
        note("last attacker" + slot, std::to_string(port.lastAttackerMark[i]), std::to_string(guest.lastAttackerMark[i]));
        note("last damaged by" + slot, slotName(port.lastDamagedBy[i]), slotName(guest.lastDamagedBy[i]));
        note("castable spells to work out" + slot, std::to_string(port.enabledMagic[i]),
             std::to_string(guest.enabledMagic[i]));
        note("damage taken" + slot, entry(port.ledger.taken[i]), entry(guest.ledger.taken[i]));
        note("damage healed" + slot, entry(port.ledger.healed[i]), entry(guest.ledger.healed[i]));
    }
    note("Black Belt targets", std::to_string(maskOf(port.blackBeltEligible)),
         std::to_string(maskOf(guest.blackBeltEligible)));
    note("shown Miss", std::to_string(maskOf(port.missed)), std::to_string(maskOf(guest.missed)));
    note("the Golem's HP", std::to_string(port.golemHp), std::to_string(guest.golemHp));
    note("the weapon's spellcast", std::to_string(port.weaponSpellcast), std::to_string(guest.weaponSpellcast));
    note("numerals", numerals(port.shown), numerals(guest.shown));
    return out;
}

void DamageOracle::writeTargetCells(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::BattleState& state = scenario.state;
    writeByte(kEffect, static_cast<std::uint8_t>(attack.effect.index() * 2));
    std::uint8_t first = kAttackFlagsAtStart;
    if (!attack.drainCapped) {
        first &= static_cast<std::uint8_t>(~kDrainCappedBit);
    }
    if (attack.canBeCritical) {
        first &= static_cast<std::uint8_t>(~kCannotBeCriticalBit);
    }
    writeByte(kFirstAttackFlags, first);
    writeByte(kAnimationCommand, 0);
    writeByte(kCommand, static_cast<std::uint8_t>(attack.command));
    writeByte(kAttackIndex, static_cast<std::uint8_t>(attack.attack));
    writeByte(kSecondary, attack.secondary);
    writeByte(kRngPosition, state.rng.position());
    writeByte(kDamageModification, attack.scratch.damageModification ? kOn : 0);
    const auto blockByte = [](const std::optional<battle::BattlerSlot>& slot) {
        return slot ? static_cast<std::uint8_t>(slotIndex(*slot)) : kNoOne;
    };
    writeByte(kGolemBlock, blockByte(attack.scratch.golemBlocked));
    writeByte(kInterceptorBlock, blockByte(attack.scratch.interceptorBlocked));
    writeByte(kForcefield, state.elementsNullifiedByForcefield.bits);
    writeByte(kAnimationPlace, 0);
    writeByte(kQueueEnd, kQueueCommandSize);
    const std::array<std::uint8_t, kQueueCommandSize> unwritten{kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    machine_.write(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize}, unwritten);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kLevel + at, battler.level);
        writeByte(kDefenses + at, battler.defense);
        writeByte(kDefenses + at + 1, battler.magicDefense);
        writeWord(kHp + at, battler.hp);
        writeWord(kMp + at, battler.mp);
        writeWord(kMaxHp + at, battler.maxHp);
        writeWord(kMaxMp + at, battler.maxMp);
        writeByte(kAbsorbedNullified + at, battler.elementsAbsorbed.bits);
        writeByte(kAbsorbedNullified + at + 1, battler.elementsNullified.bits);
        writeByte(kWeakHalved + at, battler.elementsWeak.bits);
        writeByte(kWeakHalved + at + 1, battler.elementsHalved.bits);
        std::uint8_t flags = 0;
        flags |= battler.undead ? kUndeadBit : 0;
        flags |= battler.impCritical ? kImpCriticalBit : 0;
        flags |= battler.human ? kHumanBit : 0;
        flags |= battler.nameHidden ? kNameHiddenBit : 0;
        flags |= battler.diesAtZeroMp ? kDiesAtZeroMpBit : 0;
        writeByte(kMonsterFlags + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        writeByte(kStance + at, stance);
        const auto writeStatuses = [&](std::uint32_t first12, std::uint32_t first34, const StatusSet& set) {
            writeByte(first12 + at, set.bytes[0]);
            writeByte(first12 + at + 1, set.bytes[1]);
            writeByte(first34 + at, set.bytes[2]);
            writeByte(first34 + at + 1, set.bytes[3]);
        };
        writeStatuses(kStatus12, kStatus34, battler.status);
        writeStatuses(kToSet12, kToSet34, battler.statusesToSet);
        writeStatuses(kToClear12, kToClear34, battler.statusesToClear);
        writeByte(kController + at, blockByte(battler.controlledBy));
        std::uint8_t mark = battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : 0;
        mark |= battler.retaliationPending ? kWaitingBit : 0;
        writeByte(kLastAttacker + at, mark);
        writeWord(kTaken + at, attack.scratch.ledger.taken[i].value_or(kNoDamage));
        writeWord(kHealed + at, attack.scratch.ledger.healed[i].value_or(kNoDamage));
    }
}

TargetDamageResult DamageOracle::readTargetResult() {
    TargetDamageResult result;
    const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
        if (word == kNoDamage) {
            return std::nullopt;
        }
        return word;
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        result.ledger.taken[i] = entry(readWord(kTaken + at));
        result.ledger.healed[i] = entry(readWord(kHealed + at));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.controlled[i] = (readByte(kController + at) & kWaitingBit) == 0;
        result.lastAttackerMark[i] = readByte(kLastAttacker + at);
    }
    result.stepsLeft = readByte(kMultiplier);
    result.secondary = readByte(kSecondary);
    const std::vector<std::uint8_t> place = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kQueueCommandSize});
    if (std::any_of(place.begin(), place.end(), [](std::uint8_t b) { return b != kUnwritten; })) {
        result.animationRewrittenWith = place[kQueueCommandSize - 1];
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
}

TargetDamageResult portTargetDamage(DamageScenario scenario, battle::BattlerSlot target) {
    battle::BattleScriptBuilder script;
    script.addAnimation(battle::AttackAnimation{.secondary = kUnwritten});
    battle::targetDamage(scenario.state, scenario.attack, scenario.attacker, target, script);

    TargetDamageResult result;
    result.ledger = scenario.attack.scratch.ledger;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
        result.controlled[i] = battler.controlledBy.has_value();
        std::uint8_t mark = battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : 0;
        mark |= battler.retaliationPending ? kWaitingBit : 0;
        result.lastAttackerMark[i] = mark;
    }
    result.stepsLeft = scenario.attack.multiplier;
    result.secondary = scenario.attack.secondary;
    const auto* animation = std::get_if<battle::AttackAnimation>(&script.commands().front());
    if (animation == nullptr || animation->secondary != kUnwritten) {
        result.animationRewrittenWith = animation != nullptr ? animation->secondary : 0;
    }
    result.rngPosition = scenario.state.rng.position();
    return result;
}

std::string differences(const TargetDamageResult& port, const TargetDamageResult& guest) {
    std::string out;
    const auto entry = [](const std::optional<std::uint16_t>& value) {
        return value ? std::to_string(*value) : std::string{"none"};
    };
    const auto statuses = [](const StatusSet& set) {
        return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
               "/" + std::to_string(set.bytes[3]);
    };
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("damage taken" + slot, entry(port.ledger.taken[i]), entry(guest.ledger.taken[i]));
        note("damage healed" + slot, entry(port.ledger.healed[i]), entry(guest.ledger.healed[i]));
        note("statuses to set" + slot, statuses(port.statusesToSet[i]), statuses(guest.statusesToSet[i]));
        note("statuses to clear" + slot, statuses(port.statusesToClear[i]), statuses(guest.statusesToClear[i]));
        note("controlled" + slot, std::to_string(port.controlled[i]), std::to_string(guest.controlled[i]));
        note("last attacker" + slot, std::to_string(port.lastAttackerMark[i]),
             std::to_string(guest.lastAttackerMark[i]));
    }
    note("multiplier steps", std::to_string(port.stepsLeft), std::to_string(guest.stepsLeft));
    note("animation second value", std::to_string(port.secondary), std::to_string(guest.secondary));
    note("animation rewritten with", port.animationRewrittenWith ? std::to_string(*port.animationRewrittenWith) : "none",
         guest.animationRewrittenWith ? std::to_string(*guest.animationRewrittenWith) : "none");
    note("random-number position", std::to_string(port.rngPosition), std::to_string(guest.rngPosition));
    return out;
}

void DamageOracle::writeScenario(const DamageScenario& scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    std::array<std::uint8_t, sizeof(AttackProperties)> record{};
    std::memcpy(record.data(), &attack.record, record.size());
    machine_.write(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(record.size())}, record);
    writeByte(kAttackerStat, attack.attackerStat);
    writeByte(kAttackerLevel, attack.attackerLevel);
    writeWord(kDamage, static_cast<std::uint16_t>(attack.damage));
    std::uint8_t flags = kAttackFlagsAtStart;
    if (attack.twoHanded) {
        flags &= static_cast<std::uint8_t>(~kNotTwoHanded);
    }
    if (!attack.ignoreDefenseStopsMultiplier) {
        flags &= static_cast<std::uint8_t>(~kIgnoreDefenseStopsMultiplier);
    }
    if (!attack.vanishApplies) {
        flags &= static_cast<std::uint8_t>(~kVanishAppliesBit);
    }
    writeByte(kAttackFlags, flags);
    writeByte(kMultiplier, static_cast<std::uint8_t>(attack.multiplier));

    for (std::size_t i = 0; i < scenario.state.battlers.size(); ++i) {
        const battle::Battler& battler = scenario.state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const auto first = static_cast<std::uint8_t>((battler.relicEffects1.bits & kLowSevenBits) |
                                                     (battler.relicEffects2.bits & kTopBit));
        writeByte(kRelicEffects1And3 + at, first);
        writeByte(kRelicEffects1And3 + at + 1, battler.relicEffects3.bits);
        writeByte(kRelicEffects4And5 + at, battler.relicEffects4.bits);
        writeByte(kRelicEffects4And5 + at + 1, battler.relicEffects5.bits);
    }
}

void DamageOracle::writeByte(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    machine_.write(retropp::MemoryRegion{.at = at, .size = 1}, bytes);
}

void DamageOracle::writeWord(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF),
                                            static_cast<std::uint8_t>(value >> 8)};
    machine_.write(retropp::MemoryRegion{.at = at, .size = 2}, bytes);
}

std::uint8_t DamageOracle::readByte(std::uint32_t at) {
    return machine_.read(retropp::MemoryRegion{.at = at, .size = 1})[0];
}

std::uint16_t DamageOracle::readWord(std::uint32_t at) {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{.at = at, .size = 2});
    return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8);
}

DamageScenario drawScenario(std::mt19937& rng) {
    DamageScenario s;
    s.attacker = battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
    s.attack.record.traits.bits = byte(rng);
    s.attack.record.flags1.bits = byte(rng);
    s.attack.record.flags2.bits = byte(rng);
    s.attack.record.power = byte(rng);
    s.attack.attackerStat = byte(rng);
    // A party member's level is 1 to 99; a monster's is whatever its record says.
    s.attack.attackerLevel = s.attacker.isCharacter() ? static_cast<std::uint8_t>(1 + rng() % 99) : byte(rng);
    s.attack.twoHanded = (rng() & 1) != 0;
    s.attack.ignoreDefenseStopsMultiplier = (rng() & 1) != 0;
    s.attack.multiplier = static_cast<int>(rng() % 8);
    s.attack.damage = static_cast<int>(rng() & 0xFFFF);
    battle::Battler& attacker = s.state.at(s.attacker);
    attacker.relicEffects1.bits = byte(rng);
    attacker.relicEffects2.bits = byte(rng);
    attacker.relicEffects3.bits = byte(rng);
    attacker.relicEffects4.bits = byte(rng);
    attacker.relicEffects5.bits = byte(rng);
    return s;
}

namespace {

// A party member's HP and MP never pass these.
constexpr int kPartyMaxHp = 9999;
constexpr int kPartyMaxMp = 999;
constexpr int kPartyMaxLevel = 99;
constexpr int kLedgerMax = 9999;
// No fractional attack or item in the game has a power above this.
constexpr int kMostFractionalPower = 16;

// True one time in `n`.
bool oneIn(std::mt19937& rng, unsigned n) {
    return rng() % n == 0;
}

// A byte with about a quarter of its bits set, so the flags it stands for come up one at a time.
std::uint8_t sparse(std::mt19937& rng) {
    return static_cast<std::uint8_t>(byte(rng) & byte(rng));
}

int upTo(std::mt19937& rng, int most) {
    return static_cast<int>(rng() % static_cast<unsigned>(most + 1));
}

battle::BattlerSlot anySlot(std::mt19937& rng) {
    return battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
}

}  // namespace

TargetScenario drawTargetScenario(std::mt19937& rng) {
    TargetScenario drawn;
    DamageScenario& s = drawn.scenario;
    s.attacker = anySlot(rng);
    drawn.target = anySlot(rng);

    battle::AttackInFlight& attack = s.attack;
    attack.record.traits.bits = byte(rng);
    attack.record.flags1.bits = byte(rng);
    attack.record.flags2.bits = byte(rng);
    attack.record.element.bits = oneIn(rng, 2) ? 0 : byte(rng);
    attack.record.power = oneIn(rng, 16) ? 0 : byte(rng);
    if (attack.record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE)) {
        attack.record.power = static_cast<std::uint8_t>(upTo(rng, kMostFractionalPower));
    }
    for (std::uint8_t& status : attack.record.statuses.bytes) {
        status = sparse(rng);
    }
    attack.damage = oneIn(rng, 2) ? upTo(rng, 0x3FFF) : upTo(rng, battle::kDamageWordMax);
    attack.multiplier = upTo(rng, 7);
    attack.command = oneIn(rng, 8) ? BattleCommandId::ITEM : BattleCommandId::FIGHT;
    attack.attack = static_cast<AttackId>(byte(rng));
    attack.secondary = byte(rng);
    if (s.attacker.isCharacter() && oneIn(rng, 4)) {
        attack.effect = battle::SpecialEffect::of(WeaponSpecialEffect::ATMA_WEAPON);
    }
    attack.scratch.damageModification = !oneIn(rng, 4);
    attack.drainCapped = oneIn(rng, 2);
    attack.ignoreDefenseStopsMultiplier = oneIn(rng, 2);
    if (oneIn(rng, 8)) {
        attack.scratch.golemBlocked = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    if (oneIn(rng, 8)) {
        attack.scratch.interceptorBlocked = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }

    battle::BattleState& state = s.state;
    state.elementsNullifiedByForcefield.bits = oneIn(rng, 4) ? byte(rng) : 0;
    state.rng = battle::BattleRng{byte(rng)};
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const bool party = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter();
        battler.level = party ? static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1)) : byte(rng);
        battler.maxHp = static_cast<std::uint16_t>(1 + upTo(rng, (party ? kPartyMaxHp : battle::kDamageWordMax) - 1));
        battler.hp = static_cast<std::uint16_t>(upTo(rng, battler.maxHp));
        battler.maxMp = static_cast<std::uint16_t>(upTo(rng, party ? kPartyMaxMp : battle::kDamageWordMax));
        battler.mp = static_cast<std::uint16_t>(upTo(rng, battler.maxMp));
        battler.defense = oneIn(rng, 8) ? 255 : byte(rng);
        battler.magicDefense = oneIn(rng, 8) ? 255 : byte(rng);
        battler.elementsAbsorbed.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsNullified.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsWeak.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.elementsHalved.bits = oneIn(rng, 4) ? byte(rng) : 0;
        battler.undead = oneIn(rng, 4);
        battler.impCritical = oneIn(rng, 2);
        battler.human = oneIn(rng, 2);
        battler.nameHidden = oneIn(rng, 2);
        battler.diesAtZeroMp = oneIn(rng, 2);
        battler.defending = oneIn(rng, 2);
        battler.backRow = oneIn(rng, 2);
        for (std::uint8_t& status : battler.status.bytes) {
            status = sparse(rng);
        }
        for (std::uint8_t& status : battler.statusesToSet.bytes) {
            status = static_cast<std::uint8_t>(sparse(rng) & byte(rng));
        }
        for (std::uint8_t& status : battler.statusesToClear.bytes) {
            status = static_cast<std::uint8_t>(sparse(rng) & byte(rng));
        }
        if (oneIn(rng, 4)) {
            battler.controlledBy = anySlot(rng);
        }
        if (oneIn(rng, 2)) {
            battler.lastAttacker = anySlot(rng);
        }
        battler.retaliationPending = oneIn(rng, 2);
        if (oneIn(rng, 4)) {
            attack.scratch.ledger.taken[i] = static_cast<std::uint16_t>(upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 8)) {
            attack.scratch.ledger.healed[i] = static_cast<std::uint16_t>(upTo(rng, kLedgerMax));
        }
    }
    return drawn;
}

DamageScenario drawApplyScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    attack.record.flags1.bits = oneIn(rng, 3) ? static_cast<std::uint8_t>(AttackFlag1::AFFECT_MP) : 0;
    attack.scratch.ledger = {};
    attack.scratch.weaponSpellcast.raw = oneIn(rng, 2) ? byte(rng) : 0;
    state.golemHp = oneIn(rng, 2) ? static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax)) : 0;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battle::Battler& battler = state.battlers[i];
        if (oneIn(rng, 4)) {
            battler.status.set(StatusId::ZOMBIE);
        }
        // Small pools often enough that a loss brings them to 0.
        if (oneIn(rng, 3)) {
            battler.hp = static_cast<std::uint16_t>(std::min<int>(battler.hp, upTo(rng, 64)));
            battler.mp = static_cast<std::uint16_t>(std::min<int>(battler.mp, upTo(rng, 64)));
        }
        if (oneIn(rng, 2)) {
            attack.scratch.ledger.taken[i] = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 128) : upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 4)) {
            attack.scratch.ledger.healed[i] = static_cast<std::uint16_t>(oneIn(rng, 2) ? upTo(rng, 128) : upTo(rng, kLedgerMax));
        }
        if (oneIn(rng, 4)) {
            attack.scratch.missed.add(slot);
        }
        if (slot.isMonster() && oneIn(rng, 8)) {
            state.invincible.add(slot);
        }
        if (slot.isCharacter() && oneIn(rng, 4)) {
            state.scratch.blackBeltEligible.add(slot);
        }
        if (oneIn(rng, 4)) {
            battler.lastDamagedBy = anySlot(rng);
        }
        battler.pendingUpdates.enabledMagic = oneIn(rng, 4);
    }
    return s;
}

DamageScenario drawReflectScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    AttackProperties& record = attack.record;
    // A spell the status and special-effect passes change nothing for.
    record.traits.clear(AttackTrait::PHYSICAL);
    record.flags2.clear(AttackFlag2::REMOVE_STATUS);
    record.flags2.clear(AttackFlag2::TOGGLE_STATUS);
    record.misc.clear(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    record.statuses = {};
    record.hitRate = byte(rng);
    attack.effect = {};
    // A bounce is a spell's, so the Golem and Interceptor, which step in only for Fight and
    // Capture, never block it.
    attack.command = BattleCommandId::MAGIC;
    attack.queuedCommand = attack.command;
    attack.scratch.ledger = {};
    attack.canBeCritical = oneIn(rng, 2);
    attack.vanishApplies = !oneIn(rng, 4);
    attack.scratch.quietMisses = oneIn(rng, 4);
    attack.scratch.aimExactly = oneIn(rng, 4);
    attack.scratch.targetCount = 1 + upTo(rng, 4);
    attack.scratch.weaponSpellcast.raw = oneIn(rng, 2) ? byte(rng) : 0;
    if (oneIn(rng, 2)) {
        attack.scratch.lastSpell = static_cast<AttackId>(byte(rng));
    }
    if (oneIn(rng, 4)) {
        attack.scratch.lastItem = static_cast<ItemId>(byte(rng));
    }
    const auto anySet = [&] {
        battle::BattlerSet set;
        set.characters = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x0F));
        set.monsters = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
        return set;
    };
    attack.targets = anySet();
    attack.scratch.earlierTargets = anySet();
    attack.scratch.hitInTheBack = oneIn(rng, 4) ? anySet() : battle::BattlerSet{};
    battle::AimingRules& rules = attack.rules;
    rules.noCharacterTargets = oneIn(rng, 8);
    rules.fallBackToEarlierTargets = oneIn(rng, 4);
    rules.mayTargetTheFallen = oneIn(rng, 4);
    rules.keepTargetsThatAreGone = oneIn(rng, 2);
    rules.opposingSideOnly = oneIn(rng, 2);
    rules.excludeAttacker = oneIn(rng, 8);
    // The hits to come and the battlers the spell bounced off are drawn apart: a Launcher's own
    // bounce is off its attacker, and a count left standing meets whatever bounce comes next.
    if (oneIn(rng, 3)) {
        state.extraHits = 1 + upTo(rng, 7);
    }
    if (oneIn(rng, 3)) {
        attack.params.reflectedOff = battle::BattlerSet::of(s.attacker);
    } else {
        attack.params.reflectedOff = anySet();
        if (attack.params.reflectedOff.empty()) {
            attack.params.reflectedOff.add(anySlot(rng));
        }
    }

    state.battleType = static_cast<battle::BattleType>(upTo(rng, 3));
    state.pincerSides[0] = BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F));
    state.pincerSides[1] = BattleSlotMask(static_cast<std::uint8_t>(~state.pincerSides[0].bits & 0x3F));
    state.targetableOrPresent = oneIn(rng, 4) ? anySet() : battle::BattlerSet::allCharacters() | battle::BattlerSet::allMonsters();
    state.aiUntargetable = oneIn(rng, 8) ? anySet().monstersOnly() : battle::BattlerSet{};
    state.awayFromTheField = oneIn(rng, 8) ? anySet() : battle::BattlerSet{};
    state.scratch.countersEnabled = !oneIn(rng, 4);
    attack.scratch.awayFromTheFieldUnreachable = !oneIn(rng, 4);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battle::Battler& battler = state.battlers[i];
        battler.present = !oneIn(rng, 6);
        battler.status.clear(StatusId::INVISIBLE);
        battler.status.clear(StatusId::FROZEN);
        if (oneIn(rng, 3)) {
            battler.status.set(StatusId::REFLECT);
        }
        battler.stamina = byte(rng);
        battler.evadeFactor = byte(rng);
        battler.magicBlockFactor = byte(rng);
        // The four blocks equipment can show: a dagger's, a sword's, a shield's and the Zephyr Cape's.
        battler.physicalBlockGraphics.bits = oneIn(rng, 2) ? static_cast<std::uint8_t>(sparse(rng) & kEquipmentBlocks) : 0;
        battler.magicBlockGraphics.bits = oneIn(rng, 2) ? static_cast<std::uint8_t>(sparse(rng) & kEquipmentBlocks) : 0;
        battler.instantDeathProtected = oneIn(rng, 4);
        battler.triggers.retort = oneIn(rng, 4);
        if (oneIn(rng, 8)) {
            battler.charmedBy = anySlot(rng);
        }
        if (slot.isCharacter()) {
            battler.actsAsEnemy = oneIn(rng, 8);
            if (oneIn(rng, 16)) {
                battler.seizedBy = battle::BattlerSlot::monster(upTo(rng, battle::kMonsterSlots - 1));
            }
        }
        if (oneIn(rng, 2)) {
            battler.lastHit.command = battle::Hit<BattleCommandId>{.by = anySlot(rng), .with = BattleCommandId::FIGHT};
        }
    }
    return s;
}

std::string describe(const TargetScenario& drawn) {
    const DamageScenario& s = drawn.scenario;
    const battle::AttackInFlight& a = s.attack;
    const battle::Battler& target = s.state.at(drawn.target);
    return "attacker " + std::to_string(s.attacker.index()) + " target " + std::to_string(drawn.target.index()) +
           " traits " + std::to_string(a.record.traits.bits) + " flags1 " + std::to_string(a.record.flags1.bits) +
           " flags2 " + std::to_string(a.record.flags2.bits) + " element " + std::to_string(a.record.element.bits) +
           " power " + std::to_string(a.record.power) + " damage " + std::to_string(a.damage) + " steps " +
           std::to_string(a.multiplier) + " command " + std::to_string(static_cast<int>(a.command)) + " effect " +
           std::to_string(a.effect.index()) + " modification " + std::to_string(a.scratch.damageModification) +
           " capped " + std::to_string(a.drainCapped) + " rng " + std::to_string(s.state.rng.position()) +
           " target hp " + std::to_string(target.hp) + "/" + std::to_string(target.maxHp) + " mp " +
           std::to_string(target.mp) + "/" + std::to_string(target.maxMp) + " defenses " +
           std::to_string(target.defense) + "/" + std::to_string(target.magicDefense) + " undead " +
           std::to_string(target.undead);
}

std::string describe(const DamageScenario& s) {
    const battle::Battler& attacker = s.state.at(s.attacker);
    return "attacker " + std::to_string(s.attacker.index()) + " traits " + std::to_string(s.attack.record.traits.bits) +
           " flags1 " + std::to_string(s.attack.record.flags1.bits) + " flags2 " +
           std::to_string(s.attack.record.flags2.bits) + " power " + std::to_string(s.attack.record.power) + " stat " +
           std::to_string(s.attack.attackerStat) + " level " + std::to_string(s.attack.attackerLevel) +
           " two-handed " + std::to_string(s.attack.twoHanded) + " stops " +
           std::to_string(s.attack.ignoreDefenseStopsMultiplier) + " steps " + std::to_string(s.attack.multiplier) +
           " damage " + std::to_string(s.attack.damage) + " relics " + std::to_string(attacker.relicEffects1.bits) +
           "/" + std::to_string(attacker.relicEffects2.bits) + "/" + std::to_string(attacker.relicEffects3.bits) + "/" +
           std::to_string(attacker.relicEffects4.bits) + "/" + std::to_string(attacker.relicEffects5.bits);
}

// --- the trace ---------------------------------------------------------------------------------

namespace {

// The attack's context read at each traced entry: the params block, the hits to come, the aiming
// rules, where the next params block goes in the script's buffer, and the buffer itself.
struct ContextCells {
    retropp::MemoryRegion params;
    retropp::MemoryRegion hits;
    retropp::MemoryRegion rules;
    retropp::MemoryRegion nextBlock;
    retropp::MemoryRegion blocks;
};

constexpr std::uint32_t kParamsCells = 0x7E00A0;
// The script queue's whole span, and the aiming rule set while a spell bounces off Reflect.
constexpr std::uint32_t kQueueSpan = 256;
constexpr std::uint8_t kReflectedRule = 0x10;
constexpr std::uint32_t kParamsSize = 16;
constexpr std::uint32_t kBlocksSize = 256;
// The attacker is the second byte of a params block.
constexpr std::uint32_t kBlockAttacker = 1;

std::string busAddress(std::uint32_t at) {
    char text[12];
    std::snprintf(text, sizeof text, "$%02X:%04X", (at >> 16) & 0xFF, at & 0xFFFF);
    return text;
}

std::string hexByte(std::uint8_t value) {
    char text[4];
    std::snprintf(text, sizeof text, "%02X", value);
    return text;
}

std::string framePrefix(std::uint64_t frame) {
    return "f" + std::to_string(frame);
}

}  // namespace

// A running machine answers a read with what its last completed step published, which misses what the
// step under way has written. `written` holds the last byte the guest wrote to each cell of the
// context, and a read of the context lays those over the published bytes.
struct DamageTrace::Places {
    retropp::RegionMapId<ContextCells> id;
    std::map<std::uint32_t, std::uint8_t> written{};

    std::vector<std::uint8_t> read(retropp::Vm& vm, retropp::MemoryRegion ContextCells::*key, std::uint32_t at) const {
        std::vector<std::uint8_t> bytes = vm.read(id, key);
        const std::uint32_t end = at + static_cast<std::uint32_t>(bytes.size());
        for (auto cell = written.lower_bound(at); cell != written.end() && cell->first < end; ++cell) {
            bytes[cell->first - at] = cell->second;
        }
        return bytes;
    }
};

DamageTrace::DamageTrace(retropp::Vm& machine, Line line, Frame frame, Access access)
    : line_(std::move(line)), frame_(std::move(frame)) {
    places_ = std::make_unique<Places>(Places{.id = machine.registerRegions(retropp::regions(
        retropp::region(&ContextCells::params, retropp::MemoryRegion{.at = kParamsCells, .size = kParamsSize},
                        "params block"),
        retropp::region(&ContextCells::hits, retropp::MemoryRegion{.at = kExtraHits, .size = 1}, "hits to come"),
        retropp::region(&ContextCells::rules, retropp::MemoryRegion{.at = kAimingRules, .size = 1}, "aiming rules"),
        retropp::region(&ContextCells::nextBlock, retropp::MemoryRegion{.at = kNextParamsBlock, .size = 1},
                        "next params block"),
        retropp::region(&ContextCells::blocks, retropp::MemoryRegion{.at = kParamsBlocks, .size = kBlocksSize},
                        "params blocks")))});

    retropp::EscapeMap escapes;
    for (const TracedRoutine& routine : kTracedRoutines) {
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = std::string{routine.name} + " entry",
            .at = routine.entry,
            .handler = [this, routine](retropp::Vm& vm, std::uint32_t at) {
                ++open_;
                const std::string prefix = framePrefix(frame_());
                line_(prefix + " enter " + routine.name + " " + busAddress(at));
                const std::vector<std::uint8_t> params = places_->read(vm, &ContextCells::params, kParamsCells);
                const std::uint8_t hits = places_->read(vm, &ContextCells::hits, kExtraHits)[0];
                const std::uint8_t rules = places_->read(vm, &ContextCells::rules, kAimingRules)[0];
                const std::uint8_t next = places_->read(vm, &ContextCells::nextBlock, kNextParamsBlock)[0];
                std::string context = prefix + " params";
                for (const std::uint8_t byte : params) {
                    context += " " + hexByte(byte);
                }
                context += " hits " + hexByte(hits) + " rules " + hexByte(rules) + " previous ";
                if (next >= kParamsBlockSize) {
                    const std::vector<std::uint8_t> blocks = places_->read(vm, &ContextCells::blocks, kParamsBlocks);
                    context += hexByte(blocks[next - kParamsBlockSize + kBlockAttacker]);
                } else {
                    context += "--";
                }
                line_(context);
            }});
        for (const std::uint32_t site :
             {routine.frameReturn, routine.bounceReturn, routine.otherReturns[0], routine.otherReturns[1]}) {
            if (site == 0) {
                continue;
            }
            const char* caller = site == routine.frameReturn    ? " frame return"
                                 : site == routine.bounceReturn ? " bounce return"
                                 : site == routine.otherReturns[0] ? " return 1"
                                                                   : " return 2";
            escapes.declarations.push_back(retropp::GuestEscape{
                .key = std::string{routine.name} + caller,
                .at = site,
                .handler = [this, routine](retropp::Vm&, std::uint32_t at) {
                    if (open_ > 0) {
                        --open_;
                    }
                    line_(framePrefix(frame_()) + " return " + routine.name + " " + busAddress(at));
                }});
        }
    }
    machine.registerEscapes(escapes);

    // A watch names the address the access drove; a direct-page access drives $00:00xx, and the same
    // byte of work RAM is logged under its $7E address so every line names one cell one way.
    const auto watched = [this, access](char how) {
        return [this, access, how](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
            const std::uint32_t cell = kWorkRam | (at & 0xFFFF);
            if (how == 'w' && (cell == kExtraHits || cell == kAimingRules || cell == kNextParamsBlock)) {
                places_->written[cell] = value;
            }
            if (access) {
                access(cell, value, how == 'w');
            }
            if (open_ > 0) {
                char text[48];
                std::snprintf(text, sizeof text, " %c $7E:%04X %02X", how, cell & 0xFFFF, value);
                line_(framePrefix(frame_()) + text);
            }
            return retropp::AccessVerdict::proceed();
        };
    };
    retropp::WatchMap watches;
    for (const TracedRange& range : kTracedRanges) {
        watches.declarations.push_back(retropp::GuestWatch{.key = range.key,
                                                           .at = retropp::MemoryRegion{.at = range.at, .size = range.size},
                                                           .onRead = watched('r'),
                                                           .onWrite = watched('w')});
    }
    // The context's cells outside the traced ranges: their writes are kept, and none is logged.
    const auto keepWrite = [this](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
        places_->written[kWorkRam | (at & 0xFFFF)] = value;
        return retropp::AccessVerdict::proceed();
    };
    watches.declarations.push_back(
        retropp::GuestWatch{.key = "params block",
                            .at = retropp::MemoryRegion{.at = kParamsCells, .size = kParamsSize},
                            .onWrite = keepWrite});
    watches.declarations.push_back(
        retropp::GuestWatch{.key = "params blocks",
                            .at = retropp::MemoryRegion{.at = kParamsBlocks, .size = kBlocksSize},
                            .onWrite = keepWrite});
    machine.registerWatches(watches);
}

DamageTrace::~DamageTrace() = default;

void DamageTrace::forgetContextWrites() {
    places_->written.clear();
}

void DamageTrace::returnToFrame(const TracedRoutine& routine) {
    if (open_ > 0) {
        --open_;
    }
    line_(framePrefix(frame_()) + " return " + routine.name + " " + busAddress(routine.frameReturn));
}

std::vector<TracedCall> parseTrace(std::istream& log) {
    std::vector<TracedCall> calls;
    std::optional<TracedCall> open;
    std::set<std::uint32_t> written;
    std::string text;
    int number = 0;
    const auto fail = [&](const std::string& why) {
        throw std::runtime_error("trace line " + std::to_string(number) + ": " + why + ": " + text);
    };
    const auto hex = [&](const std::string& word, std::uint32_t most) {
        std::size_t used = 0;
        unsigned long value = 0;
        try {
            value = std::stoul(word, &used, 16);
        } catch (const std::exception&) {
            fail("not a hex number");
        }
        if (used != word.size() || value > most) {
            fail("not a hex number");
        }
        return static_cast<std::uint32_t>(value);
    };
    // "$7E:3BF4" or "$C2:0B83" as one address.
    const auto address = [&](const std::string& word) {
        if (word.size() != 8 || word[0] != '$' || word[3] != ':') {
            fail("not an address");
        }
        return hex(word.substr(1, 2), 0xFF) << 16 | hex(word.substr(4), 0xFFFF);
    };
    while (std::getline(log, text)) {
        ++number;
        if (text.size() < 2 || text[0] != 'f' || !std::isdigit(static_cast<unsigned char>(text[1]))) {
            continue;
        }
        std::istringstream in{text};
        std::string frameWord;
        std::string kind;
        in >> frameWord >> kind;
        std::uint64_t frame = 0;
        try {
            frame = std::stoull(frameWord.substr(1));
        } catch (const std::exception&) {
            fail("not a frame");
        }
        if (kind == "enter") {
            std::string name;
            std::string at;
            in >> name >> at;
            address(at);
            open = TracedCall{.routine = name, .frame = frame};
            written.clear();
        } else if (kind == "params") {
            std::array<std::uint8_t, 16> params{};
            for (std::uint8_t& byte : params) {
                std::string word;
                in >> word;
                byte = static_cast<std::uint8_t>(hex(word, 0xFF));
            }
            std::string hitsWord, hits, rulesWord, rules, previousWord, previous;
            in >> hitsWord >> hits >> rulesWord >> rules >> previousWord >> previous;
            if (hitsWord != "hits" || rulesWord != "rules" || previousWord != "previous") {
                fail("not a params line");
            }
            if (open) {
                open->params = params;
                open->hitsToCome = static_cast<std::uint8_t>(hex(hits, 0xFF));
                open->aimingRules = static_cast<std::uint8_t>(hex(rules, 0xFF));
                if (previous != "--") {
                    open->previousAttacker = static_cast<std::uint8_t>(hex(previous, 0xFF));
                }
            }
        } else if (kind == "r" || kind == "w") {
            std::string at;
            std::string value;
            in >> at >> value;
            const std::uint32_t cell = address(at);
            const auto byte = static_cast<std::uint8_t>(hex(value, 0xFF));
            if (!open) {
                continue;
            }
            if (kind == "r") {
                if (!written.contains(cell) && !open->inputs.contains(cell)) {
                    open->inputs[cell] = byte;
                }
            } else {
                open->writes[cell] = byte;
                written.insert(cell);
            }
        } else if (kind == "return") {
            std::string name;
            std::string at;
            in >> name >> at;
            const std::uint32_t site = address(at);
            if (open && open->routine == name) {
                open->returnedTo = site;
                calls.push_back(std::move(*open));
                open.reset();
            }
        } else {
            fail("not a trace event");
        }
    }
    return calls;
}

// --- replaying a traced call ---------------------------------------------------------------------

namespace {

// The weapons' effects take the indexes 1 to 14, the items' $49 to $4E and the commands' $50 to $57;
// every other index is an attack's own.
constexpr std::uint8_t kLastWeaponEffect = 14;
constexpr std::uint8_t kFirstItemEffect = 0x49;
constexpr std::uint8_t kLastItemEffect = 0x4E;
constexpr std::uint8_t kItemEffectBase = 0x48;
constexpr std::uint8_t kFirstCommandEffect = 0x50;
constexpr std::uint8_t kLastCommandEffect = 0x57;

battle::SpecialEffect effectAt(std::uint8_t index) {
    if (index == 0) {
        return {};
    }
    if (index <= kLastWeaponEffect) {
        return battle::SpecialEffect::of(static_cast<WeaponSpecialEffect>(index));
    }
    if (index >= kFirstItemEffect && index <= kLastItemEffect) {
        return battle::SpecialEffect::of(static_cast<ItemUseEffect>(index - kItemEffectBase));
    }
    if (index >= kFirstCommandEffect && index <= kLastCommandEffect) {
        return battle::SpecialEffect::of(static_cast<battle::CommandEffect>(index));
    }
    return battle::SpecialEffect::of(static_cast<AttackSpecialEffect>(index));
}

// A battler named by a byte holding its slot index times two, or no one.
std::optional<battle::BattlerSlot> slotTimesTwo(std::uint8_t value) {
    if (value / 2 >= battle::kBattlerSlots) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(value / 2);
}

// The per-battler cells, each a word at twice the battler's index from its first.
constexpr std::array<std::uint32_t, 22> kBattlerCells{
    kBattlerBit, kToUpdate,  kLastDamagedBy, kController - 1, kLastAttacker, kTaken,          kHealed,
    kOnTheField, kLevel,     kDefenses,      kAbsorbedNullified, kWeakHalved, kHp,           kMp,
    kMaxHp,      kMaxMp,     DamageOracle::kRelicEffects1And3, DamageOracle::kRelicEffects4And5, kMonsterFlags, kToSet12,
    kStatus12,   kStatus34,
};

// The battler a per-battler cell belongs to, if it is one.
std::optional<int> battlerOfCell(std::uint32_t cell) {
    for (const std::uint32_t first : kBattlerCells) {
        if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
            return static_cast<int>((cell - first) / 2);
        }
    }
    for (const std::uint32_t first : {kToSet34, kToClear12, kToClear34}) {
        if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
            return static_cast<int>((cell - first) / 2);
        }
    }
    return std::nullopt;
}

const TracedRoutine& routineNamed(const std::string& name) {
    for (const TracedRoutine& routine : kTracedRoutines) {
        if (name == routine.name) {
            return routine;
        }
    }
    throw std::runtime_error("the trace names a routine it does not follow: " + name);
}

battle::BattlerSlot slotNamed(std::uint8_t index, const char* what) {
    if (index >= battle::kBattlerSlots) {
        throw std::runtime_error(std::string{what} + " names no battler: " + std::to_string(index));
    }
    return battle::BattlerSlot::fromIndex(index);
}

}  // namespace

void DamageOracle::writeContext(const DamageScenario& scenario) {
    writeByte(kParamsCells + kBlockAttacker, static_cast<std::uint8_t>(scenario.attacker.index()));
}

DamageScenario DamageOracle::readScenario() {
    DamageScenario s;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    const std::vector<std::uint8_t> record =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(sizeof(AttackProperties))});
    std::memcpy(&attack.record, record.data(), record.size());
    attack.attackerStat = readByte(kAttackerStat);
    attack.attackerLevel = readByte(kAttackerLevel);
    attack.damage = readWord(kDamage);
    const std::uint8_t flags = readByte(kAttackFlags);
    attack.twoHanded = (flags & kNotTwoHanded) == 0;
    attack.ignoreDefenseStopsMultiplier = (flags & kIgnoreDefenseStopsMultiplier) != 0;
    attack.vanishApplies = (flags & kVanishAppliesBit) != 0;
    attack.multiplier = readByte(kMultiplier);
    attack.effect = effectAt(static_cast<std::uint8_t>(readByte(kEffect) / 2));
    const std::uint8_t first = readByte(kFirstAttackFlags);
    attack.drainCapped = (first & kDrainCappedBit) != 0;
    attack.canBeCritical = (first & kCannotBeCriticalBit) == 0;
    attack.command = static_cast<BattleCommandId>(readByte(kCommand));
    attack.attack = static_cast<AttackId>(readByte(kAttackIndex));
    attack.secondary = readByte(kSecondary);
    state.rng = battle::BattleRng{readByte(kRngPosition)};
    attack.scratch.damageModification = readByte(kDamageModification) != 0;
    attack.scratch.golemBlocked = slotTimesTwo(readByte(kGolemBlock));
    attack.scratch.interceptorBlocked = slotTimesTwo(readByte(kInterceptorBlock));
    state.elementsNullifiedByForcefield.bits = readByte(kForcefield);
    attack.scratch.missed = setOf(readWord(kMissed));
    state.invincible = setOf(readWord(kInvincible));
    state.golemHp = readWord(kGolemHp);
    attack.scratch.weaponSpellcast.raw = readByte(kWeaponSpellcast);
    state.scratch.blackBeltEligible.characters =
        BattleSlotMask(static_cast<std::uint8_t>(~readByte(kBlackBeltTargets) & kPartyBits));

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        battler.level = readByte(kLevel + at);
        battler.defense = readByte(kDefenses + at);
        battler.magicDefense = readByte(kDefenses + at + 1);
        battler.hp = readWord(kHp + at);
        battler.mp = readWord(kMp + at);
        battler.maxHp = readWord(kMaxHp + at);
        battler.maxMp = readWord(kMaxMp + at);
        battler.elementsAbsorbed.bits = readByte(kAbsorbedNullified + at);
        battler.elementsNullified.bits = readByte(kAbsorbedNullified + at + 1);
        battler.elementsWeak.bits = readByte(kWeakHalved + at);
        battler.elementsHalved.bits = readByte(kWeakHalved + at + 1);
        const std::uint8_t monsterFlags = readByte(kMonsterFlags + at);
        battler.undead = (monsterFlags & kUndeadBit) != 0;
        battler.impCritical = (monsterFlags & kImpCriticalBit) != 0;
        battler.human = (monsterFlags & kHumanBit) != 0;
        battler.nameHidden = (monsterFlags & kNameHiddenBit) != 0;
        battler.diesAtZeroMp = (monsterFlags & kDiesAtZeroMpBit) != 0;
        const std::uint8_t stance = readByte(kStance + at);
        battler.defending = (stance & kDefendingBit) != 0;
        battler.backRow = (stance & kBackRowBit) != 0;
        battler.instantDeathProtected = (stance & kInstantDeathProtectedBit) != 0;
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        battler.status = statuses(kStatus12, kStatus34);
        battler.statusesToSet = statuses(kToSet12, kToSet34);
        battler.statusesToClear = statuses(kToClear12, kToClear34);
        battler.controlledBy = slotTimesTwo(readByte(kController + at));
        const std::uint8_t mark = readByte(kLastAttacker + at);
        const int lastAttacker = mark & ~kWaitingBit;
        if (lastAttacker < battle::kBattlerSlots) {
            battler.lastAttacker = battle::BattlerSlot::fromIndex(lastAttacker);
        }
        battler.retaliationPending = (mark & kWaitingBit) != 0;
        const auto entry = [](std::uint16_t word) -> std::optional<std::uint16_t> {
            if (word == kNoDamage) {
                return std::nullopt;
            }
            return word;
        };
        attack.scratch.ledger.taken[i] = entry(readWord(kTaken + at));
        attack.scratch.ledger.healed[i] = entry(readWord(kHealed + at));
        const std::uint8_t relicsLow = readByte(kRelicEffects1And3 + at);
        battler.relicEffects1.bits = static_cast<std::uint8_t>(relicsLow & kLowSevenBits);
        battler.relicEffects2.bits = static_cast<std::uint8_t>(relicsLow & kTopBit);
        battler.relicEffects3.bits = readByte(kRelicEffects1And3 + at + 1);
        battler.relicEffects4.bits = readByte(kRelicEffects4And5 + at);
        battler.relicEffects5.bits = readByte(kRelicEffects4And5 + at + 1);
        battler.pendingUpdates.enabledMagic = (readByte(kToUpdate + at) & kEnabledMagicBit) != 0;
        battler.lastDamagedBy = slotTimesTwo(readByte(kLastDamagedBy + at));
    }
    return s;
}

std::string DamageOracle::replay(const TracedCall& call) {
    const TracedRoutine& routine = routineNamed(call.routine);
    const bool bounce = routine.bounceReturn != 0 && call.returnedTo == routine.bounceReturn;
    const bool other = std::find(routine.otherReturns.begin(), routine.otherReturns.end(), call.returnedTo) !=
                       routine.otherReturns.end();
    if (!bounce && !other && call.returnedTo != routine.frameReturn) {
        throw std::runtime_error(call.routine + " returned to " + busAddress(call.returnedTo) +
                                 ", which is none of the calls the trace follows");
    }
    if (routine.entry == kUpdateStatus || routine.entry == kMagicStatusEffect) {
        return replayStatusCall(call, routine);
    }

    // The battle as the oracle writes a default scenario, then every cell the routine read as it read it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    DamageScenario s = readScenario();
    const auto input = [&](std::uint32_t cell) {
        const auto found = call.inputs.find(cell);
        return found != call.inputs.end() ? found->second : readByte(cell);
    };
    const auto overlayWrites = [&](std::uint32_t queueShift) {
        for (const auto& [cell, value] : call.writes) {
            if (cell >= kQueue && cell < kQueue + kQueueSpan && queueShift != 0) {
                if (cell - kQueue >= queueShift) {
                    writeByte(cell - queueShift, value);
                }
                continue;
            }
            writeByte(cell, value);
        }
    };

    if (routine.entry == kCalcDmg) {
        // A party member's blow reads its fourth relic effects; a monster's reads no battler's cell, and
        // a spell's answer does not depend on who cast it.
        const bool physical = s.attack.record.traits.has(AttackTrait::PHYSICAL);
        s.attacker = physical ? battle::BattlerSlot::monster(0) : battle::BattlerSlot::character(0);
        for (const auto& [cell, value] : call.inputs) {
            if (cell >= kRelicEffects4And5 && cell < kRelicEffects4And5 + 2 * battle::kBattlerSlots) {
                s.attacker = battle::BattlerSlot::fromIndex(static_cast<int>((cell - kRelicEffects4And5) / 2));
            }
        }
        const std::uint16_t port = physical ? battle::physicalDamage(s.state, s.attack, s.attacker)
                                            : battle::magicDamage(s.attack.record.power, s.attack.attackerStat,
                                                                  s.attack.attackerLevel);
        overlayWrites(0);
        const std::uint16_t guest = readWord(kDamage);
        if (port == guest) {
            return {};
        }
        return "damage word: port " + std::to_string(port) + ", cartridge " + std::to_string(guest) + "\n";
    }

    if (routine.entry == kCalcTargetDmg) {
        // The battlers whose cells the routine touched, other than `attacker`.
        const auto touchedBesides = [&](battle::BattlerSlot attacker) {
            std::set<int> touched;
            for (const auto* cells : {&call.inputs, &call.writes}) {
                for (const auto& [cell, value] : *cells) {
                    if (const std::optional<int> battler = battlerOfCell(cell)) {
                        touched.insert(*battler);
                    }
                }
            }
            touched.erase(attacker.index());
            return touched;
        };
        battle::BattlerSlot target = battle::BattlerSlot::character(0);
        if (!bounce) {
            s.attacker = slotNamed(call.params[kBlockAttacker], "the params block's attacker");
            // The target is the battler whose cells the routine touched, other than the attacker; with
            // no other, the attacker itself.
            const std::set<int> touched = touchedBesides(s.attacker);
            if (touched.size() > 1) {
                throw std::runtime_error("CalcTargetDmg touched more than one battler besides the attacker");
            }
            target = touched.empty() ? s.attacker : battle::BattlerSlot::fromIndex(*touched.begin());
        } else if ((call.aimingRules & kReflectedRule) != 0) {
            // A bounce: the lowest slot whose byte is set is the battler it bounced off this time, and its
            // byte the battler it went on to.
            int from = 0;
            while (from < battle::kBattlerSlots && call.params[static_cast<std::size_t>(from)] == kNoOne) {
                ++from;
            }
            if (from == battle::kBattlerSlots) {
                throw std::runtime_error("the bounce's block names no battler it bounced off");
            }
            s.attacker = battle::BattlerSlot::fromIndex(from);
            target = slotNamed(call.params[static_cast<std::size_t>(from)], "the bounce's target");
        } else {
            // Hits were still to come as the bounce began. The hit is made from the lowest battler in
            // the bounces that hit (bytes 14-15), which takes its bit just before the call, the bounce
            // going last slot first: for a Launcher's or Super Ball's own hits, the attacker. While
            // hits are still to come the target is in the byte the hit was given, one past the hits
            // now to come. Once they have run out a later bounce's target is in its own battler's
            // byte, so a call with none to come is the last hit (byte 0) or a later bounce, told
            // apart by whose cells the call touched.
            const battle::BattlerSet hitters =
                setOf(static_cast<std::uint16_t>(call.params[kBlockSet] | call.params[kBlockSet + 1] << 8));
            std::optional<battle::BattlerSlot> from;
            for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
                if (hitters.has(battle::BattlerSlot::fromIndex(index))) {
                    from = battle::BattlerSlot::fromIndex(index);
                }
            }
            if (!from) {
                throw std::runtime_error("a scattered hit whose block names no battler it was made from");
            }
            s.attacker = *from;
            std::vector<std::uint8_t> bytes;
            if (call.hitsToCome != kNoHitsToCome) {
                bytes.push_back(call.params[static_cast<std::size_t>(call.hitsToCome + 1) % kParamsSize]);
            } else {
                bytes.push_back(call.params[0]);
                bytes.push_back(call.params[static_cast<std::size_t>(from->index())]);
            }
            std::erase(bytes, kNoOne);
            if (bytes.empty()) {
                throw std::runtime_error("a scattered hit whose block names no target");
            }
            // With no other battler touched, a byte naming the battler the hit is made from is the
            // target.
            const std::set<int> touched = touchedBesides(s.attacker);
            target = slotNamed(bytes.front(), "the hit's target");
            for (const std::uint8_t byte : bytes) {
                if (touched.empty() ? byte == from->index() : touched.contains(byte)) {
                    target = slotNamed(byte, "the hit's target");
                }
            }
        }
        const TargetDamageResult port = portTargetDamage(s, target);
        writeScenario(s);
        writeTargetCells(s);
        overlayWrites(input(kAnimationPlace));
        return differences(port, readTargetResult());
    }

    // The damage taken and shown.
    if (!bounce) {
        s.attacker = slotNamed(call.params[kBlockAttacker], "the params block's attacker");
    } else {
        if (!call.previousAttacker) {
            throw std::runtime_error("a bounce's damage shown with no params block before it");
        }
        s.attacker = slotNamed(*call.previousAttacker, "the block before's attacker");
    }
    const std::uint8_t queueEnd = input(kQueueEnd);
    const std::uint8_t buffers = input(kNumeralBufferCount);
    const ApplyDamageResult port = portApplyDamage(s);
    writeScenario(s);
    writeTargetCells(s);
    writeApplyCells(s);
    writeByte(kQueueEnd, queueEnd);
    writeByte(kNumeralBufferCount, buffers);
    overlayWrites(0);
    return differences(port, readApplyResult(queueEnd / kQueueCommandSize, buffers));
}


// --- the statuses -----------------------------------------------------------------------------------

namespace {

// Per battler, at twice the battler's index: the statuses it can be given, its flags, its triggers,
// its four status timers and the statuses it is to have for a counterattack. A party member's
// equipment flag is one byte per party slot.
constexpr std::uint32_t kInflictable12 = kWorkRam + 0x331C;
constexpr std::uint32_t kInflictable34 = kWorkRam + 0x3330;
constexpr std::uint32_t kTriggers = kWorkRam + 0x3E4C;      // the first byte: bit 0 Retort
constexpr std::uint32_t kMoreTriggers = kWorkRam + 0x3E4D;  // bit 1 Overcast, bit 6 Phantasm
constexpr std::uint32_t kSleepTimer = kWorkRam + 0x3CF9;
constexpr std::uint32_t kStopTimer = kWorkRam + 0x3AF1;
constexpr std::uint32_t kReflectTimer = kWorkRam + 0x3F0C;
constexpr std::uint32_t kFreezeTimer = kWorkRam + 0x3F0D;
constexpr std::uint32_t kForCounter12 = kWorkRam + 0x3E60;
constexpr std::uint32_t kForCounter34 = kWorkRam + 0x3E74;
constexpr std::uint32_t kEquipmentChanged = kWorkRam + 0x2F30;
// The battle's own: the monsters standing and fallen (a byte of monster bits each), the battlers that
// died this action, the party members whose statuses changed, and the effect's misses.
constexpr std::uint32_t kMonstersStanding = kWorkRam + 0x2F2F;
constexpr std::uint32_t kMonstersFallen = kWorkRam + 0x3A3A;
constexpr std::uint32_t kDiedThisAction = kWorkRam + 0x3A56;
constexpr std::uint32_t kStatusChanged = kWorkRam + 0x3A4A;
constexpr std::uint32_t kMissedByEffect = kWorkRam + 0x3A48;

constexpr std::uint8_t kPresentBit = 0x01;
constexpr std::uint8_t kMenuMayOpenBit = 0x80;
constexpr std::uint8_t kEmptyGaugeBit = 0x40;
constexpr std::uint8_t kRetortBit = 0x01;
constexpr std::uint8_t kOvercastBit = 0x02;
constexpr std::uint8_t kPhantasmBit = 0x40;

// What is to be worked out again, as the cartridge's byte holds it.
std::uint8_t pendingByte(const battle::PendingUpdates& pending) {
    std::uint8_t bits = 0;
    bits |= pending.enabledMagic ? 0x80 : 0;
    bits |= pending.removeAllActions ? 0x40 : 0;
    bits |= pending.startCondemned ? 0x20 : 0;
    bits |= pending.stopCondemned ? 0x10 : 0;
    bits |= pending.commandList ? 0x08 : 0;
    bits |= pending.speed ? 0x04 : 0;
    bits |= pending.morph ? 0x02 : 0;
    bits |= pending.leaveTheField ? 0x01 : 0;
    return bits;
}

std::string statusesText(const StatusSet& set) {
    return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
           "/" + std::to_string(set.bytes[3]);
}

}  // namespace

void DamageOracle::writeStatusCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    writeByte(kMonstersStanding, state.monstersStanding.monsters.bits);
    writeByte(kMonstersFallen, state.monstersFallen.monsters.bits);
    writeWord(kDiedThisAction, maskOf(state.diedAwaitingCounter));
    writeWord(kStatusChanged, maskOf(state.statusChanged));
    writeWord(kMissed, maskOf(scenario.attack.scratch.missed));
    writeWord(kMissedByEffect, 0);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeByte(kInflictable12 + at, battler.inflictableStatuses.bytes[0]);
        writeByte(kInflictable12 + at + 1, battler.inflictableStatuses.bytes[1]);
        writeByte(kInflictable34 + at, battler.inflictableStatuses.bytes[2]);
        writeByte(kInflictable34 + at + 1, battler.inflictableStatuses.bytes[3]);
        std::uint8_t flags = battler.present ? kPresentBit : 0;
        flags |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        stance |= battler.emptyGaugeAfterAction ? kEmptyGaugeBit : 0;
        writeByte(kStance + at, stance);
        writeByte(kToUpdate + at, pendingByte(battler.pendingUpdates));
        writeByte(kTriggers + at, battler.triggers.retort ? kRetortBit : 0);
        std::uint8_t triggers = battler.triggers.overcast ? kOvercastBit : 0;
        triggers |= battler.triggers.phantasm ? kPhantasmBit : 0;
        writeByte(kMoreTriggers + at, triggers);
        writeByte(kSleepTimer + at, battler.sleepTimer);
        writeByte(kStopTimer + at, battler.stopTimer);
        writeByte(kReflectTimer + at, battler.reflectTimer);
        writeByte(kFreezeTimer + at, battler.freezeTimer);
        writeByte(kForCounter12 + at, battler.statusForCounter.bytes[0]);
        writeByte(kForCounter12 + at + 1, battler.statusForCounter.bytes[1]);
        writeByte(kForCounter34 + at, battler.statusForCounter.bytes[2]);
        writeByte(kForCounter34 + at + 1, battler.statusForCounter.bytes[3]);
        if (slot.isCharacter()) {
            writeByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position()), battler.equipmentChanged ? 1 : 0);
        }
    }
}

StatusChangesResult DamageOracle::statusChanges(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeStatusCells(scenario);
    call([&] { updateStatus_(kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[3]);
    return readStatusChangesResult();
}

StatusChangesResult DamageOracle::readStatusChangesResult() {
    StatusChangesResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.status[i] = readStatuses(kStatus12, kStatus34);
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.statusForCounter[i] = readStatuses(kForCounter12, kForCounter34);
        result.pendingUpdates[i] = readByte(kToUpdate + at);
        result.menuMayOpen[i] = (readByte(kOnTheField + at) & kMenuMayOpenBit) != 0;
        result.emptyGaugeAfterAction[i] = (readByte(kStance + at) & kEmptyGaugeBit) != 0;
        result.phantasm[i] = (readByte(kMoreTriggers + at) & kPhantasmBit) != 0;
        if (slot.isCharacter()) {
            result.equipmentChanged[i] = readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0;
        }
        result.sleepTimer[i] = readByte(kSleepTimer + at);
        result.stopTimer[i] = readByte(kStopTimer + at);
        result.reflectTimer[i] = readByte(kReflectTimer + at);
        result.freezeTimer[i] = readByte(kFreezeTimer + at);
    }
    result.diedAwaitingCounter = setOf(readWord(kDiedThisAction));
    result.statusChanged = setOf(readWord(kStatusChanged));
    result.monstersStanding.monsters = BattleSlotMask(readByte(kMonstersStanding));
    result.monstersFallen.monsters = BattleSlotMask(readByte(kMonstersFallen));
    return result;
}

StatusChangesResult portStatusChanges(DamageScenario scenario) {
    battle::applyStatusChanges(scenario.state);
    const battle::BattleState& state = scenario.state;
    StatusChangesResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.status[i] = battler.status;
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
        result.statusForCounter[i] = battler.statusForCounter;
        result.pendingUpdates[i] = pendingByte(battler.pendingUpdates);
        result.menuMayOpen[i] = battler.menuMayOpen;
        result.emptyGaugeAfterAction[i] = battler.emptyGaugeAfterAction;
        result.phantasm[i] = battler.triggers.phantasm;
        result.equipmentChanged[i] = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter() &&
                                     battler.equipmentChanged;
        result.sleepTimer[i] = battler.sleepTimer;
        result.stopTimer[i] = battler.stopTimer;
        result.reflectTimer[i] = battler.reflectTimer;
        result.freezeTimer[i] = battler.freezeTimer;
    }
    result.diedAwaitingCounter = state.diedAwaitingCounter;
    result.statusChanged = state.statusChanged;
    result.monstersStanding = state.monstersStanding.monstersOnly();
    result.monstersFallen = state.monstersFallen.monstersOnly();
    return result;
}

std::string differences(const StatusChangesResult& port, const StatusChangesResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("statuses" + slot, statusesText(port.status[i]), statusesText(guest.status[i]));
        note("statuses to set" + slot, statusesText(port.statusesToSet[i]), statusesText(guest.statusesToSet[i]));
        note("statuses to clear" + slot, statusesText(port.statusesToClear[i]), statusesText(guest.statusesToClear[i]));
        note("statuses for a counterattack" + slot, statusesText(port.statusForCounter[i]),
             statusesText(guest.statusForCounter[i]));
        note("to work out again" + slot, std::to_string(port.pendingUpdates[i]), std::to_string(guest.pendingUpdates[i]));
        note("menu may open" + slot, std::to_string(port.menuMayOpen[i]), std::to_string(guest.menuMayOpen[i]));
        note("gauge empties after the action" + slot, std::to_string(port.emptyGaugeAfterAction[i]),
             std::to_string(guest.emptyGaugeAfterAction[i]));
        note("Phantasm" + slot, std::to_string(port.phantasm[i]), std::to_string(guest.phantasm[i]));
        note("equipment to work out" + slot, std::to_string(port.equipmentChanged[i]),
             std::to_string(guest.equipmentChanged[i]));
        note("sleep timer" + slot, std::to_string(port.sleepTimer[i]), std::to_string(guest.sleepTimer[i]));
        note("stop timer" + slot, std::to_string(port.stopTimer[i]), std::to_string(guest.stopTimer[i]));
        note("reflect timer" + slot, std::to_string(port.reflectTimer[i]), std::to_string(guest.reflectTimer[i]));
        note("freeze timer" + slot, std::to_string(port.freezeTimer[i]), std::to_string(guest.freezeTimer[i]));
    }
    note("died this action", std::to_string(maskOf(port.diedAwaitingCounter)),
         std::to_string(maskOf(guest.diedAwaitingCounter)));
    note("statuses changed", std::to_string(maskOf(port.statusChanged)), std::to_string(maskOf(guest.statusChanged)));
    note("monsters standing", std::to_string(maskOf(port.monstersStanding)),
         std::to_string(maskOf(guest.monstersStanding)));
    note("monsters fallen", std::to_string(maskOf(port.monstersFallen)), std::to_string(maskOf(guest.monstersFallen)));
    return out;
}

StatusEffectResult DamageOracle::statusEffect(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeStatusCells(scenario);
    call([&] {
        magicStatusEffect_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                           kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[4]);
    return readStatusEffectResult(target);
}

StatusEffectResult DamageOracle::readStatusEffectResult(battle::BattlerSlot target) {
    const auto at = static_cast<std::uint32_t>(slotIndex(target));
    return StatusEffectResult{
        .statusesToSet = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1), readByte(kToSet34 + at),
                                    readByte(kToSet34 + at + 1)}},
        .statusesToClear = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                      readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}},
        .missedByEffect = setOf(readWord(kMissedByEffect)),
        .missed = setOf(readWord(kMissed)),
    };
}

DamageScenario DamageOracle::readStatusScenario() {
    DamageScenario s = readScenario();
    battle::BattleState& state = s.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        battler.inflictableStatuses = statuses(kInflictable12, kInflictable34);
        battler.statusForCounter = statuses(kForCounter12, kForCounter34);
        const std::uint8_t flags = readByte(kOnTheField + at);
        battler.present = (flags & kPresentBit) != 0;
        battler.menuMayOpen = (flags & kMenuMayOpenBit) != 0;
        battler.emptyGaugeAfterAction = (readByte(kStance + at) & kEmptyGaugeBit) != 0;
        const std::uint8_t pending = readByte(kToUpdate + at);
        battler.pendingUpdates = battle::PendingUpdates{
            .leaveTheField = (pending & 0x01) != 0,
            .enabledMagic = (pending & 0x80) != 0,
            .removeAllActions = (pending & 0x40) != 0,
            .startCondemned = (pending & 0x20) != 0,
            .stopCondemned = (pending & 0x10) != 0,
            .commandList = (pending & 0x08) != 0,
            .speed = (pending & 0x04) != 0,
            .morph = (pending & 0x02) != 0,
        };
        battler.triggers.retort = (readByte(kTriggers + at) & kRetortBit) != 0;
        const std::uint8_t triggers = readByte(kMoreTriggers + at);
        battler.triggers.overcast = (triggers & kOvercastBit) != 0;
        battler.triggers.phantasm = (triggers & kPhantasmBit) != 0;
        battler.sleepTimer = readByte(kSleepTimer + at);
        battler.stopTimer = readByte(kStopTimer + at);
        battler.reflectTimer = readByte(kReflectTimer + at);
        battler.freezeTimer = readByte(kFreezeTimer + at);
        if (slot.isCharacter()) {
            battler.equipmentChanged = readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0;
        }
    }
    state.diedAwaitingCounter = setOf(readWord(kDiedThisAction));
    state.statusChanged = setOf(readWord(kStatusChanged));
    state.monstersStanding.monsters = BattleSlotMask(readByte(kMonstersStanding));
    state.monstersFallen.monsters = BattleSlotMask(readByte(kMonstersFallen));
    return s;
}

std::string DamageOracle::replayStatusCall(const TracedCall& call, const TracedRoutine& routine) {
    // The battle as the oracle writes a default scenario, then every cell the routine read as it read it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    writeStatusCells(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    const DamageScenario s = readStatusScenario();
    // The cartridge's answer: the scenario's cells as the oracle writes them, then the routine's writes.
    const auto guestCells = [&] {
        writeScenario(s);
        writeTargetCells(s);
        writeStatusCells(s);
        for (const auto& [cell, value] : call.writes) {
            writeByte(cell, value);
        }
    };

    if (routine.entry == kUpdateStatus) {
        const StatusChangesResult port = portStatusChanges(s);
        guestCells();
        return differences(port, readStatusChangesResult());
    }

    // The target is the one battler whose cells the routine touched.
    std::set<int> touched;
    for (const auto* cells : {&call.inputs, &call.writes}) {
        for (const auto& [cell, value] : *cells) {
            if (const std::optional<int> battler = battlerOfCell(cell)) {
                touched.insert(*battler);
            }
            for (const std::uint32_t first : {kInflictable12, kInflictable34}) {
                if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
                    touched.insert(static_cast<int>((cell - first) / 2));
                }
            }
        }
    }
    if (touched.size() != 1) {
        throw std::runtime_error("MagicStatusEffect touched " + std::to_string(touched.size()) +
                                 " battlers' cells; its target is the one it touches");
    }
    const battle::BattlerSlot target = battle::BattlerSlot::fromIndex(*touched.begin());
    const StatusEffectResult port = portStatusEffect(s, target);
    guestCells();
    return differences(port, readStatusEffectResult(target));
}

StatusEffectResult portStatusEffect(DamageScenario scenario, battle::BattlerSlot target) {
    scenario.attack.scratch.missedByEffect = {};
    battle::statusEffect(scenario.state, scenario.attack, scenario.attacker, target);
    const battle::Battler& battler = scenario.state.at(target);
    return StatusEffectResult{
        .statusesToSet = battler.statusesToSet,
        .statusesToClear = battler.statusesToClear,
        .missedByEffect = scenario.attack.scratch.missedByEffect,
        .missed = scenario.attack.scratch.missed,
    };
}

std::string differences(const StatusEffectResult& port, const StatusEffectResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    note("statuses to set", statusesText(port.statusesToSet), statusesText(guest.statusesToSet));
    note("statuses to clear", statusesText(port.statusesToClear), statusesText(guest.statusesToClear));
    note("missed by the effect", std::to_string(maskOf(port.missedByEffect)), std::to_string(maskOf(guest.missedByEffect)));
    note("shown Miss", std::to_string(maskOf(port.missed)), std::to_string(maskOf(guest.missed)));
    return out;
}

namespace {

StatusSet anyStatuses(std::mt19937& rng) {
    return StatusSet{{byte(rng), byte(rng), byte(rng), byte(rng)}};
}

battle::BattlerSet anyBattlers(std::mt19937& rng) {
    return battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x0F)),
                              BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F))};
}

}  // namespace

DamageScenario drawStatusChangesScenario(std::mt19937& rng) {
    DamageScenario s;
    battle::BattleState& state = s.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const bool character = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter();
        battler.present = !oneIn(rng, 4);
        battler.status = anyStatuses(rng);
        battler.statusesToSet = anyStatuses(rng);
        battler.statusesToClear = anyStatuses(rng);
        battler.inflictableStatuses = anyStatuses(rng);
        battler.statusForCounter = anyStatuses(rng);
        battler.maxHp = static_cast<std::uint16_t>(character ? 1 + upTo(rng, kPartyMaxHp - 1) : 1 + upTo(rng, 65534));
        // Half the time the HP is within a step of an eighth of the maximum, where Near fatal turns.
        if ((rng() & 1) != 0) {
            const int eighth = battler.maxHp / 8;
            battler.hp = static_cast<std::uint16_t>(std::clamp(eighth - 1 + upTo(rng, 2), 0, static_cast<int>(battler.maxHp)));
        } else {
            battler.hp = static_cast<std::uint16_t>(upTo(rng, battler.maxHp));
        }
        battler.triggers.overcast = (rng() & 1) != 0;
        battler.triggers.phantasm = (rng() & 1) != 0;
        battler.triggers.retort = (rng() & 1) != 0;
        battler.retaliationPending = (rng() & 1) != 0;
        if ((rng() & 1) != 0) {
            battler.lastAttacker = anySlot(rng);
        }
        const std::uint8_t pending = byte(rng);
        battler.pendingUpdates = battle::PendingUpdates{
            .leaveTheField = (pending & 0x01) != 0,
            .enabledMagic = (pending & 0x80) != 0,
            .removeAllActions = (pending & 0x40) != 0,
            .startCondemned = (pending & 0x20) != 0,
            .stopCondemned = (pending & 0x10) != 0,
            .commandList = (pending & 0x08) != 0,
            .speed = (pending & 0x04) != 0,
            .morph = (pending & 0x02) != 0,
        };
        battler.menuMayOpen = (rng() & 1) != 0;
        battler.emptyGaugeAfterAction = (rng() & 1) != 0;
        battler.equipmentChanged = character && (rng() & 1) != 0;
        battler.sleepTimer = byte(rng);
        battler.stopTimer = byte(rng);
        battler.reflectTimer = byte(rng);
        battler.freezeTimer = byte(rng);
    }
    state.diedAwaitingCounter = anyBattlers(rng);
    state.statusChanged = anyBattlers(rng);
    state.monstersStanding = anyBattlers(rng).monstersOnly();
    state.monstersFallen = anyBattlers(rng).monstersOnly();
    return s;
}

TargetScenario drawStatusEffectScenario(std::mt19937& rng) {
    TargetScenario drawn;
    DamageScenario& s = drawn.scenario;
    s.attacker = anySlot(rng);
    drawn.target = anySlot(rng);
    AttackProperties& record = s.attack.record;
    record.traits.bits = byte(rng);
    record.element.bits = byte(rng);
    record.flags2.bits = byte(rng);
    // Never both status modes: no attack has them, and the cartridge would take the bytes after its
    // three-entry mode table as an address and run whatever code is there.
    if (record.flags2.has(AttackFlag2::REMOVE_STATUS) && record.flags2.has(AttackFlag2::TOGGLE_STATUS)) {
        record.flags2.clear((rng() & 1) != 0 ? AttackFlag2::REMOVE_STATUS : AttackFlag2::TOGGLE_STATUS);
    }
    record.misc.bits = byte(rng);
    record.statuses = anyStatuses(rng);
    s.attack.vanishApplies = (rng() & 1) != 0;
    s.attack.scratch.missed = anyBattlers(rng);
    battle::Battler& target = s.state.at(drawn.target);
    target.present = true;
    target.status = anyStatuses(rng);
    target.statusesToSet = anyStatuses(rng);
    target.statusesToClear = anyStatuses(rng);
    target.inflictableStatuses = (rng() & 1) != 0 ? anyStatuses(rng) : StatusSet{};
    target.undead = (rng() & 1) != 0;
    return drawn;
}

// --- the special effect on the attacker ----------------------------------------------------------

namespace {

// The direct page: the params block ($A0-$AF), the swirl's suppression ($B0 bit 4), the command's
// running state ($B1: bit 0 a counterattack, bit 2 the party cannot run).
constexpr std::uint32_t kParamsFlags = kWorkRam + 0x00A0;
constexpr std::uint32_t kParamsAttacker = kWorkRam + 0x00A1;
constexpr std::uint32_t kParamsTargets = kWorkRam + 0x00A2;
constexpr std::uint32_t kParamsHit = kWorkRam + 0x00A4;
constexpr std::uint32_t kParamsReflectedOff = kWorkRam + 0x00A6;
constexpr std::uint32_t kSwirlCell = kWorkRam + 0x00B0;
constexpr std::uint32_t kRunningCell = kWorkRam + 0x00B1;
// The command's cells: the spell to cast next, the message to show, the attacks still to make, a
// Dragon Horn's jump, Mind Blast's four picks.
constexpr std::uint32_t kFollowUpSpell = kWorkRam + 0x3400;
constexpr std::uint32_t kMessage = kWorkRam + 0x3401;
constexpr std::uint32_t kAttacksLeft = kWorkRam + 0x3A70;
constexpr std::uint32_t kDragonHorn = kWorkRam + 0x3A8E;
constexpr std::uint32_t kMindBlastPicks = kWorkRam + 0x3A5C;
// The battle's cells: Quick's turns left and its target, the end-of-battle event (its index times
// two), the battlers leaving after the action, the allies still fighting.
constexpr std::uint32_t kQuickTurns = kWorkRam + 0x3402;
constexpr std::uint32_t kQuickTarget = kWorkRam + 0x3404;
constexpr std::uint32_t kEndOfBattleEvent = kWorkRam + 0x3A6E;
constexpr std::uint32_t kLeaving = kWorkRam + 0x2F4C;
constexpr std::uint32_t kAllyCount = kWorkRam + 0x3A76;
// Per battler, at twice the battler's index: the hands' power (a party member's weapon hand, then
// its shield hand), Seize's hold, the monster's battle flags and its gold.
constexpr std::uint32_t kHandPower = kWorkRam + 0x3B68;
constexpr std::uint32_t kSeizing = kWorkRam + 0x3358;
constexpr std::uint32_t kMonsterBattleFlags = kWorkRam + 0x3C80;
constexpr std::uint32_t kGold = kWorkRam + 0x3D98;
// The battle's copy of each party member's hands, five bytes an item: the weapon hands, then the
// shield hands. The item is the first byte, the quantity the fourth.
constexpr std::uint32_t kWeaponHands = kWorkRam + 0x2B86;
constexpr std::uint32_t kShieldHands = kWorkRam + 0x2B9A;
constexpr std::uint32_t kHandItemSize = 5;
constexpr std::uint32_t kHandQuantity = 3;
// The party's gil and steps, three bytes each, and the console's frame count.
constexpr std::uint32_t kGil = kWorkRam + 0x1860;
constexpr std::uint32_t kSteps = kWorkRam + 0x1866;
constexpr std::uint32_t kFrameCount = kWorkRam + 0x021E;

constexpr std::uint8_t kParamsMonsterBit = 0x80;
constexpr std::uint8_t kParamsNoCharacterTargetsBit = 0x40;
constexpr std::uint8_t kParamsSwirlBit = 0x10;
constexpr std::uint8_t kSwirlSuppressedBit = 0x10;
constexpr std::uint8_t kCounterattackBit = 0x01;
constexpr std::uint8_t kCannotRunBit = 0x04;
constexpr std::uint8_t kInBattleMenuBit = 0x04;
constexpr std::uint8_t kAutomaticCriticalClearBit = 0x02;  // $B3 bit 1: clear for an automatic critical
constexpr std::uint8_t kRandomTargetBit = 0x40;
constexpr std::uint8_t kOpposingSideBit = 0x02;
constexpr std::uint8_t kEndTurnBit = 0x10;                  // $3A46 bit 4
constexpr std::uint8_t kDragonHornOn = 0xFF;
constexpr std::uint8_t kNoQuick = 0xFF;

void writeThreeBytes(std::array<std::uint8_t, 3>& bytes, std::uint32_t value) {
    bytes = {static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
             static_cast<std::uint8_t>((value >> 16) & 0xFF)};
}

std::string setText(battle::BattlerSet set) {
    return std::to_string(maskOf(set));
}

std::string slotText(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? std::to_string(slot->index()) : std::string{"none"};
}

std::string paramsText(const battle::AttackAnimationParams& params) {
    return "attacker " + std::to_string(params.attacker.index()) + " targets " + setText(params.targets) + " hit " +
           setText(params.targetsHit) + " reflected off " + setText(params.reflectedOff) + " swirl " +
           std::to_string(params.castingSwirl) + " monster " + std::to_string(params.attackerIsMonster) +
           " no party targets " + std::to_string(params.noCharacterTargets);
}

battle::AttackAnimationParams paramsFromBytes(std::span<const std::uint8_t> block) {
    battle::AttackAnimationParams params;
    params.attackerIsMonster = (block[0] & kParamsMonsterBit) != 0;
    params.noCharacterTargets = (block[0] & kParamsNoCharacterTargetsBit) != 0;
    params.castingSwirl = (block[0] & kParamsSwirlBit) != 0;
    params.attacker = battle::BattlerSlot::fromIndex(block[1] % battle::kBattlerSlots);
    params.targets = setOf(static_cast<std::uint16_t>(block[2] | block[3] << 8));
    params.targetsHit = setOf(static_cast<std::uint16_t>(block[4] | block[5] << 8));
    params.reflectedOff = setOf(static_cast<std::uint16_t>(block[6] | block[7] << 8));
    return params;
}

}  // namespace

void DamageOracle::writeEffectCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    const battle::AttackInFlight& attack = scenario.attack;
    const battle::AttackAnimationParams& params = attack.params;
    std::uint8_t paramsFlags = 0;
    paramsFlags |= params.attackerIsMonster ? kParamsMonsterBit : 0;
    paramsFlags |= params.noCharacterTargets ? kParamsNoCharacterTargetsBit : 0;
    paramsFlags |= params.castingSwirl ? kParamsSwirlBit : 0;
    writeByte(kParamsFlags, paramsFlags);
    writeByte(kParamsAttacker, static_cast<std::uint8_t>(params.attacker.index()));
    writeWord(kParamsTargets, maskOf(params.targets));
    writeWord(kParamsHit, maskOf(params.targetsHit));
    writeWord(kParamsReflectedOff, maskOf(params.reflectedOff));
    writeByte(kSwirlCell, attack.castingSwirlSuppressed ? kSwirlSuppressedBit : 0);
    std::uint8_t running = state.counterattackRunning ? kCounterattackBit : 0;
    running |= state.cannotRun ? kCannotRunBit : 0;
    writeByte(kRunningCell, running);
    std::uint8_t second = readByte(kAttackFlags);
    if (attack.automaticCritical) {
        second &= static_cast<std::uint8_t>(~kAutomaticCriticalClearBit);
    }
    writeByte(kAttackFlags, second);
    std::uint8_t aimFlags = attack.scratch.aimExactly ? kAimExactlyBit : 0;
    aimFlags |= state.endTurnAfterAction ? kEndTurnBit : 0;
    writeByte(kAimFlags, aimFlags);

    writeByte(kFollowUpSpell, attack.scratch.followUpSpell ? static_cast<std::uint8_t>(*attack.scratch.followUpSpell) : kNoOne);
    writeByte(kMessage, attack.scratch.message.value_or(kNoOne));
    writeByte(kAttacksLeft, static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    writeByte(kDragonHorn, attack.scratch.dragonHorn ? kDragonHornOn : 0);
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const auto& pick = attack.scratch.mindBlastPicks[position];
        writeWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2), pick ? maskOf(battle::BattlerSet::of(*pick)) : 0);
    }
    writeByte(kQuickTurns, state.quickTurnsLeft.value_or(kNoQuick));
    writeByte(kQuickTarget, state.quickTarget ? static_cast<std::uint8_t>(slotIndex(*state.quickTarget)) : kNoOne);
    writeByte(kEndOfBattleEvent,
              state.endOfBattleEvent ? static_cast<std::uint8_t>(static_cast<std::uint8_t>(*state.endOfBattleEvent) * 2) : 0);
    writeWord(kLeaving, maskOf(state.leavingAfterAction));
    writeByte(kAllyCount, static_cast<std::uint8_t>(state.allyCount));
    writeWord(kGolemHp, state.golemHp);
    writeWord(kMissed, maskOf(attack.scratch.missed));
    // The script's queue starts empty, and the params blocks after the frame's own.
    writeByte(kQueueEnd, 0);
    writeByte(kAnimationPlace, 0);

    std::array<std::uint8_t, 3> three{};
    writeThreeBytes(three, state.party.gil);
    machine_.write(retropp::MemoryRegion{.at = kGil, .size = 3}, three);
    writeThreeBytes(three, state.party.steps);
    machine_.write(retropp::MemoryRegion{.at = kSteps, .size = 3}, three);
    writeByte(kFrameCount, state.frameCount);

    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        std::uint8_t field = battler.present ? kPresentBit : 0;
        field |= battler.inBattleMenu ? kInBattleMenuBit : 0;
        field |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, field);
        writeByte(kSeizing + at, battler.seizing ? static_cast<std::uint8_t>(slotIndex(*battler.seizing)) : kNoOne);
        writeByte(kMonsterBattleFlags + at, battler.monsterFlags.bits);
        writeWord(kGold + at, battler.gold);
        if (slot.isCharacter()) {
            writeByte(kHandPower + at, battler.weaponHand.battlePower);
            writeByte(kHandPower + at + 1, battler.shieldHand.battlePower);
            const auto place = static_cast<std::uint32_t>(slot.position()) * kHandItemSize;
            const battle::BattleItem& weapon = state.inventory.weaponHands[static_cast<std::size_t>(slot.position())];
            const battle::BattleItem& shield = state.inventory.shieldHands[static_cast<std::size_t>(slot.position())];
            writeByte(kWeaponHands + place, static_cast<std::uint8_t>(weapon.item));
            writeByte(kWeaponHands + place + kHandQuantity, weapon.quantity);
            writeByte(kShieldHands + place, static_cast<std::uint8_t>(shield.item));
            writeByte(kShieldHands + place + kHandQuantity, shield.quantity);
            writeByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position()), battler.equipmentChanged ? 1 : 0);
        }
    }
}

AttackerEffectResult DamageOracle::attackerEffect(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeApplyCells(scenario);
    writeStatusCells(scenario);
    writeAimCells(scenario);
    writeEffectCells(scenario);
    call([&] {
        doAttackerEffect_(slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    return readAttackerEffectResult();
}

namespace {

// The cells both sides are read into, by name.
struct EffectCells {
    std::map<std::string, std::string> cells;
    void put(const std::string& name, const std::string& value) { cells[name] = value; }
    void put(const std::string& name, int value) { cells[name] = std::to_string(value); }
};

void putRecord(EffectCells& out, const AttackProperties& record) {
    out.put("power", record.power);
    out.put("hit rate", record.hitRate);
    out.put("traits", record.traits.bits);
    out.put("flags 1", record.flags1.bits);
    out.put("flags 2", record.flags2.bits);
}

}  // namespace

AttackerEffectResult DamageOracle::readAttackerEffectResult() {
    EffectCells out;
    std::array<std::uint8_t, sizeof(AttackProperties)> bytes{};
    const std::vector<std::uint8_t> read =
        machine_.read(retropp::MemoryRegion{.at = kRecord, .size = static_cast<std::uint32_t>(bytes.size())});
    std::copy(read.begin(), read.end(), bytes.begin());
    AttackProperties record{};
    std::memcpy(&record, bytes.data(), bytes.size());
    putRecord(out, record);
    out.put("effect", readByte(kEffect) / 2);
    out.put("damage", readWord(kDamage));
    out.put("command", readByte(kCommand));
    out.put("attack", readByte(kAttackIndex));
    out.put("second value", readByte(kSecondary));
    out.put("can be critical", (readByte(kFirstAttackFlags) & kCannotBeCriticalBit) == 0);
    out.put("automatic critical", (readByte(kAttackFlags) & kAutomaticCriticalClearBit) == 0);
    out.put("swirl suppressed", (readByte(kSwirlCell) & kSwirlSuppressedBit) != 0);
    out.put("multiplier", readByte(kMultiplier));
    const std::vector<std::uint8_t> params = machine_.read(retropp::MemoryRegion{.at = kParamsFlags, .size = 8});
    out.put("params", paramsText(paramsFromBytes(params)));
    out.put("targets", std::to_string(readWord(kTargets)));
    out.put("random target", (readByte(kAimingRules) & kRandomTargetBit) != 0);
    out.put("other side only", (readByte(kAimingRules) & kOpposingSideBit) != 0);
    const std::uint8_t spell = readByte(kFollowUpSpell);
    out.put("spell next", spell == kNoOne ? std::string{"none"} : std::to_string(spell));
    const std::uint8_t message = readByte(kMessage);
    out.put("message", message == kNoOne ? std::string{"none"} : std::to_string(message));
    out.put("damage modification", readByte(kDamageModification) != 0);
    out.put("attacks left", readByte(kAttacksLeft));
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        out.put("Mind Blast pick " + std::to_string(position),
                std::to_string(readWord(kMindBlastPicks + static_cast<std::uint32_t>(position * 2))));
    }
    out.put("shown Miss", std::to_string(readWord(kMissed)));
    const std::uint8_t hits = readByte(kExtraHits);
    out.put("hits to come", hits == kNoHitsToCome ? std::string{"none"} : std::to_string(hits + 1));
    out.put("the Golem's HP", readWord(kGolemHp));
    out.put("Forcefield", readByte(kForcefield));
    out.put("end of battle", readByte(kEndOfBattleEvent) / 2);
    const std::uint8_t quickTurns = readByte(kQuickTurns);
    out.put("Quick's turns", quickTurns == kNoQuick ? std::string{"none"} : std::to_string(quickTurns));
    out.put("Quick's target", slotText(slotTimesTwo(readByte(kQuickTarget))));
    out.put("leaving", std::to_string(readWord(kLeaving)));
    out.put("turn ends", (readByte(kAimFlags) & kEndTurnBit) != 0);
    const std::vector<std::uint8_t> gil = machine_.read(retropp::MemoryRegion{.at = kGil, .size = 3});
    out.put("gil", gil[0] | gil[1] << 8 | gil[2] << 16);
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, readWord(kHp + at));
        out.put("MP" + of, readWord(kMp + at));
        out.put("statuses to set" + of,
                statusesText(StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1), readByte(kToSet34 + at),
                                        readByte(kToSet34 + at + 1)}}));
        out.put("last attacker" + of, readByte(kLastAttacker + at));
        out.put("gold" + of, readWord(kGold + at));
        if (slot.isCharacter()) {
            const auto place = static_cast<std::uint32_t>(slot.position()) * kHandItemSize;
            out.put("hands' power" + of,
                    std::to_string(readByte(kHandPower + at)) + "/" + std::to_string(readByte(kHandPower + at + 1)));
            out.put("hands" + of, std::to_string(readByte(kWeaponHands + place)) + "x" +
                                      std::to_string(readByte(kWeaponHands + place + kHandQuantity)) + "/" +
                                      std::to_string(readByte(kShieldHands + place)) + "x" +
                                      std::to_string(readByte(kShieldHands + place + kHandQuantity)));
            out.put("equipment to work out" + of,
                    readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0);
        }
    }
    // What the effect added to the script: the animations queued, and the params blocks after the
    // frame's own.
    out.put("script animations", readByte(kQueueEnd) / kQueueCommandSize);
    const int blocks = (readByte(kNextParamsBlock) - kParamsBlockSize) / kParamsBlockSize;
    out.put("script params blocks", blocks);
    for (int block = 0; block < blocks; ++block) {
        const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{
            .at = kParamsBlocks + kParamsBlockSize * static_cast<std::uint32_t>(block + 1), .size = kParamsBlockSize});
        out.put("script params " + std::to_string(block), paramsText(paramsFromBytes(bytes)));
    }
    out.put("random-number position", readByte(kRngPosition));
    return AttackerEffectResult{.cells = std::move(out.cells)};
}

AttackerEffectRun runAttackerEffect(DamageScenario scenario) {
    AttackerEffectRun run{.after = std::move(scenario)};
    battle::attackerEffect(run.after.state, run.after.attack, run.after.attacker, run.script);
    return run;
}

AttackerEffectResult attackerEffectResult(const AttackerEffectRun& run) {
    const battle::BattleState& state = run.after.state;
    const battle::AttackInFlight& attack = run.after.attack;
    EffectCells out;
    putRecord(out, attack.record);
    out.put("effect", attack.effect.index());
    out.put("damage", static_cast<std::uint16_t>(attack.damage));
    out.put("command", static_cast<std::uint8_t>(attack.command));
    out.put("attack", static_cast<std::uint8_t>(attack.attack));
    out.put("second value", attack.secondary);
    out.put("can be critical", attack.canBeCritical);
    out.put("automatic critical", attack.automaticCritical);
    out.put("swirl suppressed", attack.castingSwirlSuppressed);
    out.put("multiplier", attack.multiplier);
    out.put("params", paramsText(attack.params));
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("random target", attack.rules.randomTarget);
    out.put("other side only", attack.rules.opposingSideOnly);
    out.put("spell next", attack.scratch.followUpSpell ? std::to_string(static_cast<int>(*attack.scratch.followUpSpell))
                                                       : std::string{"none"});
    out.put("message", attack.scratch.message ? std::to_string(*attack.scratch.message) : std::string{"none"});
    out.put("damage modification", attack.scratch.damageModification);
    out.put("attacks left", static_cast<std::uint8_t>(attack.scratch.attacksLeft));
    for (std::size_t position = 0; position < battle::kPartySlots; ++position) {
        const auto& pick = attack.scratch.mindBlastPicks[position];
        out.put("Mind Blast pick " + std::to_string(position),
                std::to_string(pick ? maskOf(battle::BattlerSet::of(*pick)) : 0));
    }
    out.put("shown Miss", std::to_string(maskOf(attack.scratch.missed)));
    out.put("hits to come", state.extraHits ? std::to_string(*state.extraHits) : std::string{"none"});
    out.put("the Golem's HP", state.golemHp);
    out.put("Forcefield", state.elementsNullifiedByForcefield.bits);
    out.put("end of battle", state.endOfBattleEvent ? static_cast<int>(*state.endOfBattleEvent) : 0);
    out.put("Quick's turns", state.quickTurnsLeft ? std::to_string(*state.quickTurnsLeft) : std::string{"none"});
    out.put("Quick's target", slotText(state.quickTarget));
    out.put("leaving", std::to_string(maskOf(state.leavingAfterAction)));
    out.put("turn ends", state.endTurnAfterAction);
    out.put("gil", static_cast<int>(state.party.gil));
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const std::string of = " of battler " + std::to_string(i);
        out.put("HP" + of, battler.hp);
        out.put("MP" + of, battler.mp);
        out.put("statuses to set" + of, statusesText(battler.statusesToSet));
        std::uint8_t mark = battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : 0;
        mark |= battler.retaliationPending ? kWaitingBit : 0;
        out.put("last attacker" + of, mark);
        out.put("gold" + of, battler.gold);
        if (slot.isCharacter()) {
            const auto position = static_cast<std::size_t>(slot.position());
            const battle::BattleItem& weapon = state.inventory.weaponHands[position];
            const battle::BattleItem& shield = state.inventory.shieldHands[position];
            out.put("hands' power" + of,
                    std::to_string(battler.weaponHand.battlePower) + "/" + std::to_string(battler.shieldHand.battlePower));
            out.put("hands" + of, std::to_string(static_cast<int>(weapon.item)) + "x" + std::to_string(weapon.quantity) +
                                      "/" + std::to_string(static_cast<int>(shield.item)) + "x" +
                                      std::to_string(shield.quantity));
            out.put("equipment to work out" + of, battler.equipmentChanged);
        }
    }
    battle::BattleScriptBuilder script = run.script;
    const std::vector<battle::BattleScriptCommand> commands = script.take();
    int animations = 0;
    std::vector<std::string> blocks;
    for (const battle::BattleScriptCommand& command : commands) {
        if (const auto* animation = std::get_if<battle::AttackAnimation>(&command)) {
            ++animations;
            blocks.push_back(paramsText(animation->params));
        }
    }
    out.put("script animations", animations);
    out.put("script params blocks", static_cast<int>(blocks.size()));
    for (std::size_t block = 0; block < blocks.size(); ++block) {
        out.put("script params " + std::to_string(block), blocks[block]);
    }
    out.put("random-number position", state.rng.position());
    return AttackerEffectResult{.cells = std::move(out.cells)};
}

std::string differences(const AttackerEffectResult& port, const AttackerEffectResult& guest) {
    std::string out;
    for (const auto& [cell, theirs] : guest.cells) {
        const auto ours = port.cells.find(cell);
        const std::string value = ours == port.cells.end() ? std::string{"(not read)"} : ours->second;
        if (value != theirs) {
            out += cell + ": port " + value + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

DamageScenario drawAttackerEffectScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetScenario(rng).scenario;
    battle::AttackInFlight& attack = s.attack;
    battle::BattleState& state = s.state;
    constexpr std::uint8_t kEffectSpace = battle::SpecialEffect::of(battle::CommandEffect::AirAnchor).index() + 1;
    attack.effect = effectAt(static_cast<std::uint8_t>(rng() % kEffectSpace));
    // A weapon's effect comes from a party member's hand.
    constexpr std::uint8_t kLastWeaponEffectIndex = 14;
    if (attack.effect.index() != 0 && attack.effect.index() <= kLastWeaponEffectIndex && s.attacker.isMonster()) {
        s.attacker = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
    }
    attack.record.hitRate = byte(rng);
    // The attacker's level as the attack loads it: a party member's 1 to 99, as every Dice and Fixed
    // Dice wielder's is.
    attack.attackerLevel = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 1));
    attack.damage = upTo(rng, battle::kDamageWordMax);
    attack.canBeCritical = oneIn(rng, 2);
    attack.automaticCritical = oneIn(rng, 4);
    attack.castingSwirlSuppressed = oneIn(rng, 2);
    attack.command = oneIn(rng, 2) ? BattleCommandId::FIGHT : static_cast<BattleCommandId>(byte(rng) % 0x20);
    attack.targets = anyBattlers(rng);
    attack.rules.randomTarget = oneIn(rng, 4);
    attack.rules.opposingSideOnly = oneIn(rng, 2);
    attack.params.attacker = s.attacker;
    attack.params.attackerIsMonster = s.attacker.isMonster();
    attack.params.targets = anyBattlers(rng);
    attack.params.targetsHit = oneIn(rng, 2) ? attack.params.targets : anyBattlers(rng);
    attack.params.noCharacterTargets = attack.params.targets.charactersOnly().empty();
    attack.params.castingSwirl = oneIn(rng, 2);
    attack.scratch.targetCount = 1 + upTo(rng, battle::kBattlerSlots - 1);
    attack.scratch.attacksLeft = upTo(rng, 3);
    attack.scratch.dragonHorn = oneIn(rng, 8);
    attack.scratch.missed = oneIn(rng, 4) ? anyBattlers(rng) : battle::BattlerSet{};
    if (oneIn(rng, 4)) {
        attack.scratch.message = static_cast<std::uint8_t>(byte(rng) % kNoOne);
    }
    if (oneIn(rng, 4)) {
        attack.scratch.followUpSpell = static_cast<AttackId>(byte(rng) % 0xFF);
    }

    state.party.gil = oneIn(rng, 4) ? static_cast<std::uint32_t>(upTo(rng, 5000))
                                    : static_cast<std::uint32_t>(upTo(rng, static_cast<int>(kMaxGil)));
    state.party.steps = static_cast<std::uint32_t>(upTo(rng, static_cast<int>(kMaxSteps)));
    state.frameCount = byte(rng);
    state.counterattackRunning = oneIn(rng, 4);
    state.cannotRun = oneIn(rng, 2);
    state.allyCount = upTo(rng, battle::kPartySlots);
    state.golemHp = oneIn(rng, 2) ? static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax)) : 0;
    state.endTurnAfterAction = oneIn(rng, 4);
    state.awayFromTheField = oneIn(rng, 4) ? anyBattlers(rng) : battle::BattlerSet{};
    if (oneIn(rng, 2)) {
        state.quickTurnsLeft = static_cast<std::uint8_t>(upTo(rng, 2));
        state.quickTarget = anySlot(rng);
    }
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        battler.present = !oneIn(rng, 4);
        battler.inBattleMenu = oneIn(rng, 4);
        battler.menuMayOpen = oneIn(rng, 2);
        battler.monsterFlags.bits = sparse(rng);
        battler.gold = oneIn(rng, 4) ? 0 : static_cast<std::uint16_t>(upTo(rng, battle::kDamageWordMax));
        if (oneIn(rng, 2)) {
            battler.status.set(StatusId::REFLECT);
        }
        if (oneIn(rng, 4)) {
            battler.seizing = battle::BattlerSlot::character(upTo(rng, battle::kPartySlots - 1));
        }
        if (slot.isCharacter()) {
            battler.weaponHand.battlePower = byte(rng);
            battler.shieldHand.battlePower = byte(rng);
            battler.equipmentChanged = oneIn(rng, 4);
            const auto position = static_cast<std::size_t>(slot.position());
            const auto hand = [&] {
                return battle::BattleItem{.item = oneIn(rng, 2) ? ItemId::OGRE_NIX : static_cast<ItemId>(byte(rng)),
                                          .quantity = 1};
            };
            state.inventory.weaponHands[position] = hand();
            state.inventory.shieldHands[position] = hand();
        }
    }
    return s;
}

}  // namespace ostinato::trace
