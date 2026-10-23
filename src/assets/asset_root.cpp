#include "assets/asset_root.h"

#include <algorithm>
#include <iostream>
#include <system_error>

#include "retropp/asset_registry.h"
#include "retropp/save_store.h"
#include "retropp/user_files.h"

namespace ostinato::assets {

std::optional<std::filesystem::path> developmentAssetRoot(
    const std::filesystem::path& executableDir, const std::filesystem::path& projectRoot) {
    if (projectRoot.empty() || executableDir.empty()) {
        return std::nullopt;
    }

    // Walking components rather than comparing strings is what makes a sibling directory whose
    // name shares the project's prefix a miss: "/w/OstinatoVI-old" and "/w/OstinatoVI" differ at
    // their last component, where a string prefix test would call the first one inside the second.
    const auto [rootEnd, dirEnd] =
        std::mismatch(projectRoot.begin(), projectRoot.end(), executableDir.begin(),
                      executableDir.end());
    (void)dirEnd;
    if (rootEnd != projectRoot.end()) {
        return std::nullopt;
    }
    return projectRoot;
}

void configureAssetRoot(const std::optional<std::filesystem::path>& projectRoot) {
    if (projectRoot) {
        // assetRoot() is the engine's default at this point: an absolute path to the executable's
        // directory, resolved by EngineConfig::setActive. Both sides are canonicalized so that
        // symlinks and `..` cannot make an inside path look like an outside one.
        std::error_code ec;
        const std::filesystem::path here =
            std::filesystem::weakly_canonical(retropp::assetRoot(), ec);
        const std::filesystem::path root = std::filesystem::weakly_canonical(*projectRoot, ec);
        if (!ec) {
            if (const auto devRoot = developmentAssetRoot(here, root)) {
                retropp::setAssetRoot(*devRoot);
                return;
            }
        }
    }

    // The per-user directory. Resolving it creates it, so it is writable by the time an install
    // needs it.
    try {
        retropp::setAssetRoot(retropp::UserFiles{}.root());
    } catch (const retropp::SaveStoreError& error) {
        std::cerr << "Could not resolve the per-user data directory (" << error.what()
                  << "). Falling back to the program's own directory, which works but is not "
                     "where your files belong.\n";
    }
}

}  // namespace ostinato::assets
