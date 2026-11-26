// The text a report shows a value as: a battler's slot, a set of battlers, statuses, a params
// block, the numerals shown, a bounce, a last-attacker mark, a work-RAM address, and one line per
// cell that differs. differences() and describe() build their lines with these, in namespace
// detail.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

#include "battle/battle_script.h"
#include "battle/battler_set.h"
#include "data/text_corpus.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/bytes.h"
#include "scenarios/ending/rewards.h"

namespace ostinato::trace::detail {

using namespace cells;

std::string slotText(battle::BattlerSlot slot);

std::string shownText(const battle::ReflectedAttackShown& shown);

std::string shownText(const battle::ScatteredHitsShown& shown);

// A bounce off Reflect ($09) and a Launcher's or Super Ball's hits ($15) as the queue's four-byte
// command and the params block it plays with hold them: the attack in the command's third byte; the
// block's first ten bytes the battler each slot's bounce went on to, or the targets one per hit from
// the highest, $FF for none; bytes 10-13 a bounce's blocks per party slot; bytes 14-15 the bounces that
// hit, or the attacker of the hits (GfxCmd_09 btlgfx_main.asm:25033-25243, GfxCmd_15 :25249-25367).
battle::ReflectedAttackShown reflectedShown(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block);
battle::ScatteredHitsShown scatteredShown(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block);

// A bounce or a Launcher's hits, from the queue's command and its block, as shownText() gives it.
std::string bounceText(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block);

std::string busAddress(std::uint32_t at);

std::string statusesText(const StatusSet& set);

std::string setText(battle::BattlerSet set);

std::string slotText(const std::optional<battle::BattlerSlot>& slot);

std::string paramsText(const battle::AttackAnimationParams& params);

std::string cellDifferences(const std::map<std::string, std::string>& port,
                            const std::map<std::string, std::string>& guest);

void addLine(std::string& out, const char* field, int port, int guest);

std::string messageText(const MessageShown& shown);

std::string markText(const std::optional<battle::BattlerSlot>& slot);

const TextCorpus& noText();

}  // namespace ostinato::trace::detail
