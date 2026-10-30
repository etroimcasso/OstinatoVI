// MonsterDeathFade — a dying monster's sprites as they fade out.
//
//     Sprites monster = MonsterSprite(key, x, y, mirrored, image, dying ? deathColors : palette, 0);
//     if (dying) monster = MonsterDeathFade(std::move(monster), *fade);
//     SpriteLayer(key, depth, {monster});
//
// In the death palette the monster is added over what lies behind it, and every channel of its
// colors is taken down by one 31st a frame: none on the first frame in the death palette, 31 on the
// last, where every color is black and adds nothing. The darkening lands on the monster's own
// pixels only; its see-through pixels stay see-through.
#pragma once

#include "battle/presentation.h"
#include "render/types.h"

namespace ostinato::render {

// PRECONDITION (asserted): the fade is in the death palette.
[[nodiscard]] Sprites MonsterDeathFade(Sprites monster, const battle::DeathFade& fade);

}  // namespace ostinato::render
