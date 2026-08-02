# OstinatoVI

A native C++ reimplementation of **Final Fantasy VI** (Super Famicom / SNES), built on the
[Retro++ engine](https://github.com/etroimcasso/GBCPP-Engine). The goal is behavioral
fidelity — same observable behavior as the original given the same inputs and RNG state —
running as ordinary native code on Windows, macOS, and Linux. Not an emulator, and not a
mechanical ASM translation: idiomatic modern C++ against a portable engine layer.

**Status: early infrastructure.** The build system, test harness, and asset scaffolding are in
place; game systems are not yet ported. The current binary prints the engine version and exits.

## What ships and what doesn't

- **You supply your own ROM.** On first launch the app asks you to select your legitimately
  owned FF6/FF3(US) ROM and extracts the assets it needs locally — the same model as Zelda64
  recompiled or Ship of Harkinian. (This flow is future work; see
  [`docs/features/asset-acquisition.md`](docs/features/asset-acquisition.md).)
- **No copyrighted content is committed to or distributed with this repository.** No ROMs, no
  extracted graphics/audio, no dialogue text. `.gitignore` bans ROM extensions and extracted
  asset content tree-wide.

## Repository layout

| Path | What it is |
|---|---|
| `src/`, `include/ff6/` | Port source and public headers |
| `tests/` | GoogleTest suite |
| `engine/` | [Retro++](https://github.com/etroimcasso/GBCPP-Engine) engine submodule (currently a private repository — see note below) |
| `original-src/` | [everything8215/ff6](https://github.com/everything8215/ff6) SNES disassembly submodule (GPL-3.0) — read-only derivation reference; development-time only, never required by the distribution |
| `assets/{gfx,audio}/default/` | Canonical asset-pack load targets; contents are generated locally and never committed |
| `docs/` | Project documentation (design context, feature docs) |
| `scripts/` | Development tooling |

> **Note on submodules:** the Retro++ engine repository is private while it stabilizes, so
> third-party clones cannot initialize `engine/` yet and the project will not build externally
> until the engine is published. The `original-src/` disassembly submodule is public but is only
> needed for development; much of its data corpus is generated locally by its `make rip` step
> against a user-supplied ROM.

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

The port's licensing is being finalized. The upstream disassembly
([everything8215/ff6](https://github.com/everything8215/ff6)) is GPL-3.0 and is used as a
design/derivation reference; the Retro++ engine carries its own license. Final Fantasy VI is the
property of Square Enix — this project ships no Square Enix-copyrighted content and requires the
user's own ROM.
