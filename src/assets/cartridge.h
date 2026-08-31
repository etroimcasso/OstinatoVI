// The player's cartridge: recognising one, keeping it, and reading the game's content out of it.
//
// Nothing the game says or draws is compiled into this program. The script, the graphics, and the
// tables all live in a Final Fantasy VI cartridge, and the player supplies one. It is copied into
// their own files once, so they need not keep the original where the game can find it, and every
// launch reads what it needs straight out of that copy.
//
// The route, start to finish:
//
//   1. installCartridge — identify the image and write it into the player's files. Once, ever.
//   2. ingestCartridge  — host the copy on a VM, read each family, tear the VM down.
//
// There is no third way in. Nothing is decoded to intermediate files, so there is no half-populated
// state to detect and no second code path that only developers exercise: a developer, a CI runner,
// and a player all take the two steps above.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "data/text_corpus.h"
#include "data/world_map.h"
#include "ostinato/game_version.h"

namespace ostinato::assets {

// --- recognising an image ----------------------------------------------------------------------

// The image proper, with a copier header dropped if the dump carries one. A header is recognised
// by length alone — a headered dump is exactly kCopierHeaderBytes longer than a cartridge — which
// is what the upstream tooling asks a player to do by hand before extracting.
[[nodiscard]] std::span<const std::uint8_t> stripCopierHeader(std::span<const std::uint8_t> image);

// Which revision `image` is, or nothing when it is not one this port accepts. The image must
// already be headerless — pass it through stripCopierHeader first.
//
// Identification is the CRC32 of the whole image, the same check the upstream extraction tooling
// makes, so a modified or mis-dumped cartridge is refused rather than half-read.
[[nodiscard]] std::optional<GameVersion> identifyRom(std::span<const std::uint8_t> image);

// --- keeping one -------------------------------------------------------------------------------

// What one install attempt did. `message` is player-facing either way: on a refusal it says which
// cartridges are accepted, and on success which one was recognised.
struct InstallResult {
    bool succeeded = false;
    std::optional<GameVersion> version{};
    std::string message;
};

// Copy `image` into the player's files under `root`, if it is a cartridge this port accepts.
//
// The copy is written atomically, so an interrupted install leaves the previous cartridge intact
// rather than a truncated one. A refused image writes nothing.
[[nodiscard]] InstallResult installCartridge(std::span<const std::uint8_t> image,
                                             const std::filesystem::path& root);

// The same, reading the image from a file. A file that cannot be read is a refusal with the reason
// in `message`.
[[nodiscard]] InstallResult installCartridge(const std::filesystem::path& romPath,
                                             const std::filesystem::path& root);

// Whether a cartridge has already been installed under `root`.
[[nodiscard]] bool cartridgeInstalled(const std::filesystem::path& root);

// --- reading one -------------------------------------------------------------------------------

// Everything one launch reads out of the cartridge.
//
// The spans inside stay valid for the life of the program: the bytes are held by the engine's data
// library, which resolves an entry once and never evicts it.
struct IngestedContent {
    GameVersion version{};
    TextCorpus text;
    WorldTilePool worldTiles;
};

// Read a cartridge image already in memory: host it on a VM, take the families the game needs, and
// let the VM go. The machine exists only for the duration of the call.
//
// Throws rather than returning something empty — a cartridge that cannot be read is a real failure
// and an empty corpus would look like a game with nothing to say. std::runtime_error reports an
// image that is not an accepted revision, and the machine reports its own failures.
[[nodiscard]] IngestedContent ingestCartridge(std::span<const std::uint8_t> image);

// The same, reading the cartridge the player installed. This is the shipped path; the overload
// above is how a test hands over an image without one being installed.
//
// std::runtime_error names the cartridge when the file cannot be read.
[[nodiscard]] IngestedContent ingestCartridge();

}  // namespace ostinato::assets
