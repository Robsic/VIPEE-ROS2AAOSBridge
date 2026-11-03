#!/usr/bin/env bash
# Copia o probe EVS (android/evs_probe) e o fork da HAL evs-v4l2
# (android/evs_hal, AD-016) para a árvore AOSP. Escreve em
# $ANDROID_BUILD_TOP: na árvore local só com aprovação (AGENTS.md §5).
# Depois: vm_m.sh vipee_evs_probe vipee-evs-v4l2 VipeeEvsCameraOverlay
set -euo pipefail

AOSP="${ANDROID_BUILD_TOP:-$HOME/aosp}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EMU="$AOSP/device/generic/car/emulator"

rm -rf "$EMU/vipee_evs_probe" "$EMU/vipee_evs_hal" "$EMU/vipee_evs_rro"
mkdir -p "$EMU/vipee_evs_probe"
cp "$REPO"/android/evs_probe/{Android.bp,*.cpp} "$EMU/vipee_evs_probe/"
cp -r "$REPO/android/evs_hal" "$EMU/vipee_evs_hal"
cp -r "$REPO/android/evs/rro" "$EMU/vipee_evs_rro"
find "$EMU/vipee_evs_probe" "$EMU/vipee_evs_hal" "$EMU/vipee_evs_rro" -type f | sort
