#!/usr/bin/env bash
# Copia o probe EVS (android/evs_probe) para a árvore AOSP. Escreve em
# $ANDROID_BUILD_TOP: na árvore local só com aprovação (AGENTS.md §5).
# Depois: vm_m.sh vipee_evs_probe android.hardware.automotive.evs-v4l2
set -euo pipefail

AOSP="${ANDROID_BUILD_TOP:-$HOME/aosp}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$AOSP/device/generic/car/emulator/vipee_evs_probe"

rm -rf "$DST"
mkdir -p "$DST"
cp "$REPO"/android/evs_probe/{Android.bp,*.cpp} "$DST/"
find "$DST" -type f | sort
