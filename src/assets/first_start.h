// The first-start flow: what happens when the game launches and no cartridge has been installed.
//
// The game ships with none of its own content — every word and every picture comes out of a Final
// Fantasy VI cartridge. Rather than failing with an error that sends the player off to run a
// command, it asks on the spot: a native file-selection dialog, the install in process, and then
// the ordinary launch.
//
// The sequence lives entirely in the port, ahead of engine construction:
//
//   1. Is a cartridge installed? (cartridge.h)
//   2. If not, ask the player to point at theirs.
//   3. Install it — identify, then copy it into their files.
//   4. Check again, then carry on into ordinary startup.
//
// It is the same question `--install-rom` answers headlessly for a developer or a CI runner, asked
// at the same point of startup and answered by the same code.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "assets/cartridge.h"

namespace ostinato::assets {

// Which of the three ways the dialog can end actually happened. SDL reports a failure and a
// cancellation through the same callback, distinguished only by whether the file list is null, and
// conflating them tells a player whose dialog is broken that they declined.
enum class DialogEnd { Chosen, Cancelled, Failed };

// What one attempt at the dialog produced. `path` is meaningful only when `end` is Chosen.
struct DialogResult {
    DialogEnd end = DialogEnd::Cancelled;
    std::filesystem::path path;
};

// Show the platform's native file-selection dialog and block until the player chooses a file or
// dismisses it.
//
// Must be called from the main thread. Initializes SDL's video subsystem if it is not already up
// (the dialog is a platform window) and pumps events while it waits, which is what lets the
// portal-based dialogs on Linux complete. The subsystem is deliberately left up afterwards — see
// the note at the definition.
[[nodiscard]] DialogResult promptForCartridge();

// Run the whole sequence: check, and if nothing is installed, ask and install. Returns true when a
// cartridge is installed afterwards and startup may continue.
//
// A cancelled dialog re-prompts once with the consequence stated, because the first dismissal is as
// often a misclick as a decision; a second dismissal is taken as an answer. A dialog that FAILED is
// not re-prompted — it will fail again.
//
// `report` receives player-facing text — one line or many — for whatever the caller wants to do
// with it (stderr at startup today, an on-screen panel once there is a window).
[[nodiscard]] bool ensureCartridgeInstalled(
    const std::function<void(const std::string&)>& report);

// The decision half of the above, with the three things that need a machine injected: the presence
// check, one dialog attempt, and the install. The shipped overload binds the real ones; the tests
// bind fakes and drive every path — first launch, cancel, re-prompt, broken dialog, refused
// cartridge, a write that did not land — without a window, a cartridge, or a filesystem.
[[nodiscard]] bool ensureCartridgeInstalled(
    const std::function<void(const std::string&)>& report,
    const std::function<bool()>& installed,
    const std::function<DialogResult()>& askForCartridge,
    const std::function<InstallResult(const std::filesystem::path&)>& install);

}  // namespace ostinato::assets
