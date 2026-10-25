#include "render/tile_layer.h"

#include <map>
#include <span>
#include <string>
#include <utility>

#include "render/layers.h"

namespace ostinato::render {

namespace {

// The cells each layer was last handed, by key.
std::map<std::string, Cells, std::less<>>& keptCells() {
    static std::map<std::string, Cells, std::less<>> kept;
    return kept;
}

}  // namespace

retropp::DrawLayer TileLayer(std::string_view key, std::int32_t z, Cells cells, int widthInTiles,
                             int heightInTiles, LayerOptions options) {
    auto& kept = keptCells();
    auto found = kept.find(key);
    if (found == kept.end()) {
        found = kept.emplace(std::string{key}, Cells{}).first;
    }
    found->second = std::move(cells);

    return retropp::DrawLayer{
        .key = key,
        .z = z,
        .size = kScreenSize,
        .scroll = options.scroll,
        .alpha = options.alpha,
        .blend = options.blend,
        .content = retropp::TileContent{.widthInTiles = widthInTiles,
                                        .heightInTiles = heightInTiles,
                                        .cells = std::span<const retropp::TileCell>(found->second),
                                        .wrap = retropp::TileWrap::Blank},
        .effects = std::move(options.effects),
        .regions = std::move(options.regions),
        .transform = options.transform,
        .transformEdge = options.transformEdge,
    };
}

}  // namespace ostinato::render
