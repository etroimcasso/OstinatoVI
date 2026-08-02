# Feature Status Registry

One row per feature. Statuses: ⬜ planned · 🟡 in progress · ✅ complete · ⏸ deferred · ❌ dropped. Per-feature docs live in `docs/features/` with filenames matching the Feature column.

| Feature | Status | Doc | Notes |
|---|---|---|---|
| asset-acquisition | ⬜ planned (doc authored) | `features/asset-acquisition.md` | Pack model + populate paths locked at inception; populate script done; extraction tool is its own later work |
| build-system | ✅ complete | `features/build-system.md` | CMake + `add_subdirectory(engine)`; 3 targets; GoogleTest 1/1/0; lean 52 KB binary; dev populate. Engine pin `7c3707a` |
| ci | ⬜ planned | `features/ci.md` (at CI standup) | Multi-platform self-hosted runners; corpus access via runner-local ROM (DESIGN.md open question #2, resolved) |
| audio-engine (SNES) | ⬜ planned | `features/audio-engine.md` (at engine kickoff) | Engine-side: SPC700 + S-DSP backend designed against this consumer; dual-backend mandatory |
| extraction-tool | ⬜ planned | (own doc when work starts) | Runtime ROM extraction; license determination first (GPL upstream reference) |
