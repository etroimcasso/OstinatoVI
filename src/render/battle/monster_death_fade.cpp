#include "render/battle/monster_death_fade.h"

#include <cassert>
#include <cstdint>

#include "data/cartridge_color.h"
#include "render/layers.h"

namespace ostinato::render {

Sprites MonsterDeathFade(Sprites monster, const battle::DeathFade& fade) {
    assert(fade.inDeathPalette() && "the fade has not reached the death palette");
    const std::uint8_t level = CartridgeColor::expandChannel(static_cast<unsigned>(fade.darkening()));
    for (retropp::Sprite& sprite : monster) {
        sprite.blend = retropp::BlendMode::Add;
        sprite.regions = {retropp::Region{
            .key = kDeathFadeKey,
            .effects = {retropp::ScreenSpaceEffect{.kind = retropp::ScreenSpaceEffectKind::ColorFill,
                                                   .fill = retropp::Rgba8{.r = level, .g = level, .b = level}}},
            .blend = retropp::BlendMode::Subtract,
        }};
    }
    return monster;
}

}  // namespace ostinato::render
