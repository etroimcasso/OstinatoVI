#!/usr/bin/env bash
#
# setup-dev-assets.sh — dev-time populate of the canonical asset packs.
#
# Stages raw rip products VERBATIM (no format conversion) from the two
# unambiguously typed upstream modules into the two canonical pack targets:
#
#     original-src/src/gfx   -> assets/gfx/default/     (bitplanes, palettes, screens, lz)
#     original-src/src/sound -> assets/audio/default/   (brr samples, sfx/song data, spc driver)
#
# Every other upstream module produces .dat game-data tables that later phases
# parse into C++ directly from original-src/ — they are not presentation packs.
#
# assets/*/default/* is gitignored (only .gitkeep is tracked), so this script
# never stages anything into version control. Re-runnable: it clears prior
# staged content (keeping the tracked .gitkeep) before copying.
#
# Prerequisite: `make rip` has been run in original-src/ against a vanilla ROM
# placed in original-src/vanilla/. This script aborts with guidance if not.
#
# macOS / bash only — dev populate is a dev-machine path.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

src_root="${repo_root}/original-src/src"
gfx_src="${src_root}/gfx"
audio_src="${src_root}/sound"
gfx_dst="${repo_root}/assets/gfx/default"
audio_dst="${repo_root}/assets/audio/default"

require_rip() {
    local label="$1" glob="$2"
    if ! compgen -G "${glob}" > /dev/null 2>&1; then
        echo "ERROR: no rip products found for ${label}." >&2
        echo "       Expected e.g.: ${glob}" >&2
        echo "       Run 'make rip' in original-src/ against a vanilla ROM in" >&2
        echo "       original-src/vanilla/, then re-run this script." >&2
        exit 1
    fi
}

stage() {
    local label="$1" src="$2" dst="$3"
    mkdir -p "${dst}"
    # Clear previously staged content; keep the tracked .gitkeep placeholder.
    find "${dst}" -mindepth 1 ! -name '.gitkeep' -delete
    # Copy the module's contents verbatim.
    cp -R "${src}/." "${dst}/"
    local count
    count="$(find "${dst}" -type f ! -name '.gitkeep' | wc -l | tr -d ' ')"
    echo "  ${label}: staged ${count} files -> ${dst#"${repo_root}/"}"
}

echo "Staging dev asset packs from original-src/ rip products..."

require_rip "graphics" "${gfx_src}/*.4bpp"
require_rip "audio"    "${audio_src}/sample_brr/*.brr"

stage "graphics" "${gfx_src}"   "${gfx_dst}"
stage "audio"    "${audio_src}" "${audio_dst}"

echo "Done. (assets/*/default/ contents are gitignored — nothing was staged into git.)"
