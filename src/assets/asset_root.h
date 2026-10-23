// Where the player's cartridge lives, and where LoadFromPath assets resolve from.
//
// The cartridge is the player's own: read out of a game they own, belonging to them and to this
// machine, so it lives in the per-user data directory beside their saves — the same place
// UserFiles and SaveStore resolve. That is one directory wherever the binary itself happens to
// sit, and it is writable, which the application directory of a real install frequently is not.
//
// A development build bakes its project tree in, so a developer's binary reads and writes the
// cartridge inside their checkout instead of the per-user data directory. The path is fixed at
// compile time, and a binary can be copied off the machine that built it — or just to another
// directory on the same one. Pointing a moved binary at its build tree is wrong twice over: it
// looks for a cartridge somewhere the player will never put one, and the install then WRITES
// there, filling a directory unrelated to where the program is running.
//
// The rule: the project tree belongs to the binary still sitting inside it. Anything else keeps
// the player's own directory, which is where their cartridge belongs.
#pragma once

#include <filesystem>
#include <optional>

namespace ostinato::assets {

// The asset root to adopt for a binary whose executable lives in `executableDir`, given the
// project root baked in at build time — or nothing, meaning the binary is not inside its build
// tree.
//
// Answers the project root when `executableDir` is inside it (or is it), and nothing otherwise.
// Comparison is per path component, so a sibling directory whose name merely begins with the
// project's does not count as being inside it. Both paths are taken as given; a caller that wants
// symlinks and `..` resolved should normalize before calling.
[[nodiscard]] std::optional<std::filesystem::path> developmentAssetRoot(
    const std::filesystem::path& executableDir, const std::filesystem::path& projectRoot);

// Set the engine's asset root for this run: `projectRoot` when the running binary still sits
// inside it (developmentAssetRoot), otherwise the per-user data directory. Every program that
// reads the cartridge — the game and the developer previews — calls this once at startup, so they
// all read the same one.
//
// `projectRoot` is the tree a development build bakes in; pass nothing for a build without one.
// Call it after retropp::EngineConfig::setActive: that publishes the identity the per-user
// directory is resolved from, and sets the default root (the executable's directory) this reads.
//
// When the per-user directory cannot be resolved — an identity the engine never published, or a
// platform that cannot answer — this says so on standard error and leaves the engine's default
// root in place, so the run continues with the files beside the program.
void configureAssetRoot(const std::optional<std::filesystem::path>& projectRoot);

}  // namespace ostinato::assets
