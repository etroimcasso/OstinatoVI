#!/usr/bin/env bash
# macOS runner dependency installer (ericmacmini — ARM64 Apple Silicon).
# Run once on the runner machine. Requires Homebrew. Xcode Command Line Tools provide clang.
# SDL3 on macOS needs no extra system libraries — the engine builds it from its own submodule.
set -euo pipefail

brew update
brew install cmake ninja

echo "Done. Verify: cmake --version && ninja --version && clang --version"
