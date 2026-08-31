#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "retropp/asset_registry.h"
#include "retropp/engine_config.h"
#include "retropp/save_store.h"
#include "retropp/user_files.h"
#include "retropp/version.h"

#include "assets/asset_root.h"
#include "assets/cartridge.h"
#include "assets/first_start.h"
#include "ostinato/ostinato.h"

namespace {

// Where the player's cartridge lives, and where LoadFromPath assets resolve from.
//
// The cartridge is the player's own: read out of a game they own, belonging to them and to this
// machine, so it lives in the per-user data directory beside their saves — the same place
// UserFiles and SaveStore resolve. That is one directory wherever the binary itself happens to
// sit, and it is writable, which the application directory of a real install frequently is not.
//
// A development build reads its project tree instead, so a developer exercises the shipped route
// against the cartridge in their checkout. That applies only to a binary still inside that tree:
// developmentAssetRoot decides, and assets/asset_root.h explains why a binary that has been moved
// must not keep it.
void configureAssetRoot() {
#ifdef OSTINATO_PROJECT_ROOT
    // assetRoot() is the engine's default at this point: an absolute path to the executable's
    // directory, resolved by EngineConfig::setActive. Both sides are canonicalised so that symlinks
    // and `..` cannot make an inside path look like an outside one.
    std::error_code ec;
    const std::filesystem::path here =
        std::filesystem::weakly_canonical(retropp::assetRoot(), ec);
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::path{OSTINATO_PROJECT_ROOT}, ec);
    if (!ec) {
        if (const auto devRoot = ostinato::assets::developmentAssetRoot(here, root)) {
            retropp::setAssetRoot(*devRoot);
            return;
        }
    }
#endif

    // The per-user directory. Resolving it creates it, so it is writable by the time the install
    // needs it. It can still fail — an identity the engine never published, or a platform that
    // cannot answer — and that is worth saying out loud rather than aborting on: the engine's own
    // default still resolves, so the run continues with the files beside the program instead.
    try {
        retropp::setAssetRoot(retropp::UserFiles{}.root());
    } catch (const retropp::SaveStoreError& error) {
        std::cerr << "Could not resolve the per-user data directory (" << error.what()
                  << "). Falling back to the program's own directory, which works but is not "
                     "where your files belong.\n";
    }
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

    configureAssetRoot();

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
