# Feature Status Registry

One row per feature. Statuses: ⬜ planned · 🟡 in progress · ✅ complete · ⏸ deferred · ❌ dropped. Per-feature docs live in `docs/features/` with filenames matching the Feature column.

| Feature | Status | Doc | Notes |
|---|---|---|---|
| asset-acquisition | ✅ complete | `features/asset-acquisition.md` | The cartridge is copied once into the player's files and read in place on every launch |
| build-system | ✅ complete | `features/build-system.md` | CMake + `add_subdirectory(engine)`; 7 targets (lib / binary / smoke + enum + data + spell + item test runners); GoogleTest 34/36/2; lean binary. Engine pin `7c3707a` |
| ci | ✅ complete | `features/ci.md` | 6 self-hosted jobs (5 platform builds + parser unit tests; the ARM64 trio serialized on its shared host); `ci/**` push trigger; runner-local ROM staged + ripped for rip-product e2e; Unix jobs build in the workspace and stage the build dir to `/tmp` |
| audio-engine (SNES) | ⬜ planned | `features/audio-engine.md` (at engine kickoff) | Engine-side: SPC700 + S-DSP backend designed against this consumer; dual-backend mandatory |
| extraction-tool | ⬜ planned | (own doc when work starts) | Runtime ROM extraction; license determination first (GPL upstream reference) |
| text-engine | ✅ complete | `engine/text/dialog.md` | The field dialog text engine. Logic (state machine, control codes, English word wrap, splices, choices, page wipe, map title) pinned over the whole English script; the dialog font, widths and choice arrow read from the cartridge; the rendering components (`engine/rendering/components.md`) draw text, arrow, wipe and title; the developer preview `ostinato-preview-dialog` (`tools/previews/dialog/`) types any message from the installed cartridge in a window. The dialog window around the text is its own feature |
| dialog-window | ✅ complete | `engine/text/window.md` | The field dialog window. Logic (open and close frame by frame, top or bottom placement, text-only messages, the map-title box, where the text area sits and its line spacing — 16 px while the box opens, 15 once open) pinned over the whole English script; the box drawn as its own tile layer beneath the text from the cartridge's wallpaper art and colors, shaded, squashed while it opens and cropped while it closes; formation and shading tables checked against the cartridge |
