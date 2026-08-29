// Whether a build's baked project root applies to the binary that is running.
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
// symlinks and `..` resolved should normalise before calling.
[[nodiscard]] std::optional<std::filesystem::path> developmentAssetRoot(
    const std::filesystem::path& executableDir, const std::filesystem::path& projectRoot);

}  // namespace ostinato::assets
