#include "data/text_offsets.h"

#include <cassert>

namespace ostinato {

namespace {

// The generated offset arrays (constexpr std::uint32_t kXxxOffsets[N]).
#include "data/generated/text_offsets_data.inc"

}  // namespace

std::span<const std::uint32_t> dialogueOffsets() { return kDialogueOffsets; }

std::span<const std::uint32_t> pointerOffsets(TextClass klass) {
    switch (klass) {
        case TextClass::ATTACK_MSG:       return kAttackMsgOffsets;
        case TextClass::BATTLE_DLG:       return kBattleDlgOffsets;
        case TextClass::MONSTER_DLG:      return kMonsterDlgOffsets;
        case TextClass::MAP_TITLE:        return kMapTitleOffsets;
        case TextClass::ITEM_DESC:        return kItemDescOffsets;
        case TextClass::MAGIC_DESC:       return kMagicDescOffsets;
        case TextClass::LORE_DESC:        return kLoreDescOffsets;
        case TextClass::BLITZ_DESC:       return kBlitzDescOffsets;
        case TextClass::BUSHIDO_DESC:     return kBushidoDescOffsets;
        case TextClass::GENJU_ATTACK_DESC: return kGenjuAttackDescOffsets;
        case TextClass::GENJU_BONUS_DESC: return kGenjuBonusDescOffsets;
        case TextClass::RARE_ITEM_DESC:   return kRareItemDescOffsets;
        default:
            assert(false && "pointerOffsets: not a self-contained pointer class "
                             "(dialogue uses dialogueOffsets())");
            return {};
    }
}

}  // namespace ostinato
