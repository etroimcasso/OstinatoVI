// A battle's start checked whole: the cartridge's start against the port's, cell by cell, with the
// write-set, carried-read and read-set gates.

#include "oracle/damage_oracle.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace ostinato::trace {

namespace {

std::string cellLine(const char* what, const UnwrittenRead& read) {
    char text[96];
    std::snprintf(text, sizeof text, "%s $%02X:%04X (%02X)\n", what, (read.at >> 16) & 0xFF, read.at & 0xFFFF,
                  read.value);
    return text;
}

// A gate's lines under its name, or nothing when it has none.
std::string named(const char* gate, const std::string& lines) {
    return lines.empty() ? std::string{} : std::string{gate} + ":\n" + lines;
}

}  // namespace

const MonsterSprites& DamageOracle::monsterSprites() const {
    if (monsterSprites_ == nullptr) {
        throw std::logic_error("the oracle was handed no monsters' sprites (DamageOracle::useMonsterSprites)");
    }
    return *monsterSprites_;
}

const TextCorpus& DamageOracle::text() const {
    if (text_ == nullptr) {
        throw std::logic_error("the oracle was handed no game text (DamageOracle::useText)");
    }
    return *text_;
}

StartCheck DamageOracle::checkStart(const StartScenario& scenario) {
    const TextCorpus& corpus = text();
    StartCheck check{.differences = {}, .cartridge = battleStart(scenario),
                     .port = portStart(scenario, corpus, monsterSprites())};
    std::string& lines = check.differences;
    if (!check.cartridge.reachedTheLoop) {
        lines += "the cartridge's start did not reach the loop's first frame wait\n";
        return check;
    }
    if (!check.cartridge.returned) {
        lines += "the cartridge's battle did not return after its end-of-battle event\n";
    }
    lines += compareBattleState(check.port, scenario.cells, check.cartridge.cells);
    lines += named("write-set gate", uncoveredWrites(check.port, scenario.cells, check.cartridge.battleWrites));
    lines += named("carried-read gate", carriedReads(check.port, scenario.cells, check.cartridge.readsAfterThePoint));
    std::string unhanded;
    for (const UnwrittenRead& read : check.cartridge.unwrittenReads) {
        if (!leftAsTheMachineHasIt(read.at)) {
            unhanded += cellLine("read before written:", read);
        }
    }
    for (const UnwrittenRead& read : check.cartridge.readsBeyondTheBattlesCells) {
        unhanded += cellLine("read past the battle's cells:", read);
    }
    lines += named("read-set gate", unhanded);
    return check;
}

}  // namespace ostinato::trace
