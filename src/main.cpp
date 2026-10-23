#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "retropp/asset_registry.h"
#include "retropp/engine_config.h"
#include "retropp/version.h"

#include "assets/asset_root.h"
#include "assets/cartridge.h"
#include "assets/first_start.h"
#include "ostinato/ostinato.h"

namespace {

// The project tree a development build reads its cartridge from while the binary still sits inside
// it (assets/asset_root.h); a build without one reads the per-user directory.
std::optional<std::filesystem::path> bakedProjectRoot() {
#ifdef OSTINATO_PROJECT_ROOT
    return std::filesystem::path{OSTINATO_PROJECT_ROOT};
#else
    return std::nullopt;
#endif
}

// The headless half of the first-start flow: identify a cartridge and keep it, with no dialog and
// no window. A developer and a CI runner take this route; a player takes the dialog to the same
// install.
int installFromCommandLine(int argc, char** argv) {
    std::filesystem::path romPath;
    std::filesystem::path outRoot;
    bool haveRom = false;
    bool haveOut = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--install-rom" && i + 1 < argc) {
            romPath = argv[++i];
            haveRom = true;
        } else if (arg == "--out" && i + 1 < argc) {
            outRoot = argv[++i];
            haveOut = true;
        } else {
            std::cerr << "usage: ostinato-vi --install-rom <path> [--out <dir>]\n";
            return 1;
        }
    }

    if (!haveRom) {
        std::cerr << "usage: ostinato-vi --install-rom <path> [--out <dir>]\n";
        return 1;
    }

    const ostinato::assets::InstallResult result =
        ostinato::assets::installCartridge(romPath, haveOut ? outRoot : retropp::assetRoot());
    std::cout << result.message << "\n";
    return result.succeeded ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    // The identity is the one required field; everything else takes the engine's defaults. It comes
    // first even for --install-rom, and it has to: the per-user directory the cartridge is written
    // into is resolved from this identity, so publishing it is what tells the install where the
    // file belongs.
    const retropp::EngineConfig config{
        .identity = {.organization = "OstinatoVI", .application = "OstinatoVI"},
        .window = {.title = "Ostinato VI"},
    };
    retropp::EngineConfig::setActive(config);

    ostinato::assets::configureAssetRoot(bakedProjectRoot());

    // Installing a cartridge is part of getting the game running, not a separate tool, so it lives
    // behind a flag on this binary rather than a second executable. It is answered here — after the
    // asset root is settled, and before anything that would read a cartridge — at the same point
    // the first-start flow below asks a player the same question.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--install-rom") {
            return installFromCommandLine(argc, argv);
        }
    }

    // Everything the game says and draws comes out of the player's cartridge, so a first launch has
    // none. Ask for it and keep it before anything tries to read one.
    if (!ostinato::assets::ensureCartridgeInstalled(
            [](const std::string& line) { std::cerr << line; })) {
        return 1;
    }

    std::cout << ostinato::portName() << " on Polyrhythm " << retropp::version() << "\n";
    return 0;
}
