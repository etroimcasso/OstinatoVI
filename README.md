# Ostinato VI

A native C++ reimplementation of **Final Fantasy VI** (Super Famicom / SNES), built on the
[Polyrhythm engine](https://github.com/RetroPlusPlus/Polyrhythm). The goal is behavioral
fidelity — same observable behavior as the original given the same inputs and RNG state —
running as ordinary native code on Windows, macOS, and Linux. Not an emulator, and not a
mechanical ASM translation: idiomatic modern C++ against a portable engine layer.

**Status: early infrastructure.** The build system, test harness, and asset scaffolding are in
place; game systems are not yet ported. The current binary prints the engine version and exits.

## Version support

The port targets **all three releases** of the game: Final Fantasy VI 1.0 (Japan), Final
Fantasy III 1.0 (US), and Final Fantasy III 1.1 (US). The ROM you supply determines the
version and language — a Japanese ROM yields the Japanese localization (kanji/kana text and
all), a US ROM the English one. One binary covers all three; version-specific behavior is
carried explicitly rather than baked to a single release. Current development validates
against FF3 1.1 (US), with the other two slotted in as verification ROMs become available.

## What this is not

This project is not an alternative to the official **Pixel Remaster** line, and it is not
trying to compete with it. If you want a modernized Final Fantasy VI — updated visuals,
rearranged audio, and quality-of-life conveniences — buy the Pixel Remaster and give
Square Enix your money for their efforts.

For the same reason, this port will not be adding the convenience features common to
modern re-releases and emulators: **no speed-up, no encounter disabling, no widescreen.**
The point of the project is the original game, behaving as it did on the SNES, running as
native code on modern platforms — fidelity is the feature, and gameplay-altering
conveniences work against it.

## What ships and what doesn't

- **You supply your own ROM.** On first launch the app asks you to select your legitimately
  owned FF6 (J) or FF3 (US) ROM and extracts the assets it needs locally — the same model as
  Zelda64 recompiled or Ship of Harkinian. (This flow is future work; see
  [`docs/features/asset-acquisition.md`](docs/features/asset-acquisition.md).)
- **No copyrighted content is committed to or distributed with this repository.** No ROMs, no
  extracted graphics/audio, no dialogue text. `.gitignore` bans ROM extensions and extracted
  asset content tree-wide.

## Repository layout

| Path | What it is |
|---|---|
| `src/`, `include/ostinato/` | Port source and public headers |
| `tests/` | GoogleTest suite |
| `engine/` | [Polyrhythm](https://github.com/RetroPlusPlus/Polyrhythm) engine submodule |
| `original-src/` | [everything8215/ff6](https://github.com/everything8215/ff6) SNES disassembly submodule (GPL-3.0) — read-only derivation reference; development-time only, never required by the distribution |
| `assets/{gfx,audio}/default/` | Canonical asset-pack load targets; contents are generated locally and never committed |
| `docs/` | Project documentation (design context, feature docs, and the [developer guide](docs/engine/README.md) to the shipped surfaces) |
| `scripts/` | Development tooling |

> **Note on submodules:** both submodules are public and initialize without credentials. The
> `original-src/` disassembly is needed only for development; much of its data corpus is
> generated locally by its `make rip` step against a user-supplied ROM.

## Building

Requires CMake 3.28+, a C++20 compiler (GCC 13+ / Clang 16+ / AppleClang 15+ / MSVC 19.38+),
and recursive submodules:

```sh
git clone --recursive git@github.com:etroimcasso/OstinatoVI.git
cd OstinatoVI
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build
```

The build defaults to a lean Release configuration (optimized, dead-stripped, symbol-stripped).

## License

This port's source is licensed under the **GNU Affero General Public License v3.0** (AGPL-3.0);
see [LICENSE](LICENSE). Copyright © 2026 the OstinatoVI authors. The Polyrhythm engine (the
`engine/` submodule) is likewise AGPL-3.0, so the combined distributable is AGPL-3.0.

The upstream disassembly ([everything8215/ff6](https://github.com/everything8215/ff6)) is GPL-3.0
and is used only as a design/derivation reference — AGPL-3.0 remains compatible with it. Final
Fantasy VI is the property of Square Enix; this project ships no Square Enix-copyrighted content
and requires the user's own ROM.
