#include "assets/first_start.h"

#include <atomic>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>

#include "retropp/asset_registry.h"

namespace ostinato::assets {
namespace {

namespace fs = std::filesystem;

// How many times the player is asked. The first dismissal is as often a misclick or a "wait, where
// did I put it" as a decision, so it is worth one more ask with the consequence spelled out. A
// second dismissal is an answer.
constexpr int kDialogAttempts = 2;

// What the dialog callback hands back to the waiting main thread. SDL may invoke the callback on
// another thread, so the path is written under the mutex and `finished` is the release/acquire
// handshake the wait loop spins on.
struct DialogOutcome {
    std::mutex lock;
    std::string path;
    DialogEnd end = DialogEnd::Cancelled;
    std::string error;
    std::atomic<bool> finished{false};
};

void onFileChosen(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* outcome = static_cast<DialogOutcome*>(userdata);
    {
        const std::lock_guard guard{outcome->lock};
        if (filelist == nullptr) {
            // The dialog itself failed. SDL_GetError is only meaningful here.
            outcome->end = DialogEnd::Failed;
            const char* why = SDL_GetError();
            outcome->error = (why != nullptr) ? why : "";
        } else if (filelist[0] == nullptr) {
            outcome->end = DialogEnd::Cancelled;
        } else {
            outcome->end = DialogEnd::Chosen;
            outcome->path = filelist[0];
        }
    }
    outcome->finished.store(true, std::memory_order_release);
}

}  // namespace

DialogResult promptForCartridge() {
    // The dialog is a platform window, so the video subsystem has to be up. There is no window of
    // this game's own at this point in startup — the dialog is parentless, which every supported
    // platform allows.
    //
    // SDL refcounts subsystem initialization, so asking for video here is safe whether or not it is
    // already up. It is deliberately NOT torn down afterwards: the panel's completion handler is
    // still unwinding when the callback returns, and quitting the subsystem out from under it hangs
    // the process on macOS. Video is the engine's from here on anyway.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::cerr << "Could not start SDL video, so no file dialog can be shown: "
                  << SDL_GetError() << "\n";
        return {DialogEnd::Failed, {}};
    }

    const SDL_DialogFileFilter filters[]{
        {"Super Nintendo ROM", "sfc;smc"},
        {"All files", "*"},
    };

    // The dialog says what it is FOR, not just "Open". The plain SDL_ShowOpenFileDialog carries no
    // title, so the properties form of the same dialog is used; a platform that cannot display a
    // title shows its stock chrome, which is the same dialog minus the words.
    DialogOutcome outcome;
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(
        props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
        const_cast<SDL_DialogFileFilter*>(static_cast<const SDL_DialogFileFilter*>(filters)));
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          static_cast<Sint64>(std::size(filters)));
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                          "Locate your Final Fantasy VI cartridge");
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, onFileChosen, &outcome, props);

    // Block until the callback fires. Pumping events is required, not merely polite: the
    // portal-based dialogs on Linux run over DBus and never complete without it.
    while (!outcome.finished.load(std::memory_order_acquire)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);

    const std::lock_guard guard{outcome.lock};
    if (outcome.end == DialogEnd::Chosen) {
        return {DialogEnd::Chosen, fs::path{outcome.path}};
    }
    if (outcome.end == DialogEnd::Failed) {
        std::cerr << "The file dialog could not be opened: "
                  << (outcome.error.empty() ? "no reason given" : outcome.error) << "\n";
    }
    return {outcome.end, {}};
}

bool ensureCartridgeInstalled(
    const std::function<void(const std::string&)>& report,
    const std::function<bool()>& installed,
    const std::function<DialogResult()>& askForCartridge,
    const std::function<InstallResult(const std::filesystem::path&)>& install) {
    if (installed()) {
        return true;
    }
    report(
        "No cartridge is installed yet. Final Fantasy VI's text and graphics come out of the "
        "cartridge, so one is needed before the game can start.\n");

    fs::path chosen;
    for (int attempt = 0; attempt < kDialogAttempts; ++attempt) {
        const DialogResult dialog = askForCartridge();
        if (dialog.end == DialogEnd::Chosen) {
            chosen = dialog.path;
            break;
        }
        if (dialog.end == DialogEnd::Failed) {
            // Asking again would open the same broken dialog, so this ends here rather than
            // spending the player's second attempt on a certainty.
            report(
                "The file dialog could not be opened, so there is no way to ask for your "
                "cartridge.\nThe game cannot start.\n");
            return false;
        }
        const bool lastAttempt = (attempt + 1 == kDialogAttempts);
        if (lastAttempt) {
            report(
                "No cartridge was chosen, so there is nothing to read the game from.\n"
                "The game cannot start.\n");
            return false;
        }
        report(
            "No cartridge was chosen. The game has no content without one, so it will ask once "
            "more.\n");
    }

    const InstallResult result = install(chosen);
    report(result.message + "\n");
    if (!result.succeeded) {
        return false;
    }

    // Check again rather than taking the install's word for it: everything that follows only works
    // if the cartridge is actually on disk.
    if (!installed()) {
        report("The cartridge was installed but cannot be found.\nThe game cannot start.\n");
        return false;
    }
    return true;
}

bool ensureCartridgeInstalled(const std::function<void(const std::string&)>& report) {
    return ensureCartridgeInstalled(
        report, [] { return cartridgeInstalled(retropp::assetRoot()); },
        [] { return promptForCartridge(); },
        [](const fs::path& rom) { return installCartridge(rom, retropp::assetRoot()); });
}

}  // namespace ostinato::assets
