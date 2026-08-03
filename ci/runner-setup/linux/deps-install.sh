#!/usr/bin/env bash
# Linux runner dependency installer (beefserve — Ubuntu, x64).
# Run once on the runner machine as root. Safe to re-run (apt is idempotent).
#
# These are the system libraries the Retro++ engine's SDL3 build links against.
# The engine builds SDL3 and SameBoy from its own submodules, so no SDL/SameBoy
# package is needed — only the platform dev headers SDL3 compiles against.
set -euo pipefail

apt-get update -y
apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libwayland-dev \
    libxkbcommon-dev \
    libegl-dev \
    libgles2-mesa-dev \
    libpulse-dev \
    libasound2-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxfixes-dev \
    libxi-dev \
    libxtst-dev \
    libvulkan-dev \
    python3-venv \
    python3-numpy

# python3-venv + python3-numpy: the parser-tests job stages the vanilla ROM and
# runs the upstream rip (tools/extract_assets.py), which imports numpy. With
# python3-numpy present the job uses the system interpreter directly; the venv
# capability is its fallback for runners where system numpy is absent.

echo "Done. Verify: cmake --version && ninja --version && gcc --version"
