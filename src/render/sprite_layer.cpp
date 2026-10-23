#include "render/sprite_layer.h"

#include <map>
#include <span>
#include <string>
#include <utility>

#include "render/layers.h"

namespace ostinato::render {

namespace {

// The sprites each layer was last handed, by key.
std::map<std::string, Sprites, std::less<>>& keptSprites() {
    static std::map<std::string, Sprites, std::less<>> kept;
    return kept;
}

}  // namespace

retropp::DrawLayer SpriteLayer(std::string_view key, std::int32_t z,
                               std::initializer_list<Sprites> children, LayerOptions options) {
    auto& kept = keptSprites();
    auto found = kept.find(key);
    if (found == kept.end()) {
        found = kept.emplace(std::string{key}, Sprites{}).first;
    }
    Sprites& sprites = found->second;
    sprites.clear();
    for (const Sprites& child : children) {
        sprites.insert(sprites.end(), child.begin(), child.end());
    }

    return retropp::DrawLayer{
        .key = key,
        .z = z,
        .size = kScreenSize,
        .scroll = options.scroll,
        .alpha = options.alpha,
        .blend = options.blend,
        .content = retropp::SpriteContent{.sprites = std::span<const retropp::Sprite>(sprites)},
        .effects = std::move(options.effects),
        .regions = std::move(options.regions),
        .transform = options.transform,
        .transformEdge = options.transformEdge,
    };
}

}  // namespace ostinato::render
