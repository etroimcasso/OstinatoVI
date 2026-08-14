#!/usr/bin/env bash
# Dev-only: populate assets/text/{en,jp}/ from the local original-src rip.
#
# The game reads raw text `.dat` files from a language directory
# (assets/text/en/<stem>.dat); the rip produces language-suffixed files
# (original-src/src/text/<stem>_en.dat). This script copies the rip products
# into the game's read layout, stripping the language suffix (the language is
# the directory). It is port-owned — a plain file copy, nothing derived from
# the upstream disassembly's tooling.
#
# The copied contents are gitignored and never committed. The shipped end-user
# extraction tool (a later phase) writes the identical layout via byte-range
# copies from the user's ROM.
#
# Usage: tools/populate_text_assets.sh [SOURCE_ROOT]
#   SOURCE_ROOT defaults to ./original-src

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_root="${1:-${repo_root}/original-src}"
src_text="${source_root}/src/text"
dst_en="${repo_root}/assets/text/en"
dst_jp="${repo_root}/assets/text/jp"

if [[ ! -d "${src_text}" ]]; then
    echo "error: ${src_text} not found — run 'make rip' in original-src first" >&2
    exit 1
fi

mkdir -p "${dst_en}" "${dst_jp}"

copied_en=0
for f in "${src_text}"/*_en.dat; do
    [[ -e "${f}" ]] || continue
    stem="$(basename "${f}" _en.dat)"
    cp "${f}" "${dst_en}/${stem}.dat"
    copied_en=$((copied_en + 1))
done

copied_jp=0
for f in "${src_text}"/*_jp.dat; do
    [[ -e "${f}" ]] || continue
    stem="$(basename "${f}" _jp.dat)"
    cp "${f}" "${dst_jp}/${stem}.dat"
    copied_jp=$((copied_jp + 1))
done

echo "populated ${copied_en} EN + ${copied_jp} JP text file(s) into assets/text/"
