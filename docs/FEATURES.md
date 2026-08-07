# Feature Status Registry

One row per feature. Statuses: ⬜ planned · 🟡 in progress · ✅ complete · ⏸ deferred · ❌ dropped. Per-feature docs live in `docs/features/` with filenames matching the Feature column.

| Feature | Status | Doc | Notes |
|---|---|---|---|
| asset-acquisition | ⬜ planned (doc authored) | `features/asset-acquisition.md` | Pack model + populate paths locked at inception; populate script done; extraction tool is its own later work |
| build-system | ✅ complete | `features/build-system.md` | CMake + `add_subdirectory(engine)`; 7 targets (lib / binary / smoke + enum + data + spell + item test runners); GoogleTest 34/36/2; lean binary; dev populate. Engine pin `7c3707a` |
| ci | ✅ complete | `features/ci.md` | 6 self-hosted jobs (5 platform builds + parser unit tests; the ARM64 trio serialized on its shared host); `ci/**` push trigger; runner-local ROM staged + ripped for rip-product e2e; Unix jobs build in the workspace and stage the build dir to `/tmp` |
| audio-engine (SNES) | ⬜ planned | `features/audio-engine.md` (at engine kickoff) | Engine-side: SPC700 + S-DSP backend designed against this consumer; dual-backend mandatory |
| extraction-tool | ⬜ planned | (own doc when work starts) | Runtime ROM extraction; license determination first (GPL upstream reference) |
