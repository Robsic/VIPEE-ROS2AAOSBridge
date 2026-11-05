#!/usr/bin/env bash
# Copia o VHAL da ponte (android/vhal) e o wire.h para a árvore AOSP. Escreve
# em $ANDROID_BUILD_TOP: na árvore local só com aprovação (AGENTS.md §5); na VM
# de build é livre. Depois: m vipee-vehicle-service vipee_properties.json
set -euo pipefail

AOSP="${ANDROID_BUILD_TOP:-$HOME/aosp}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$AOSP/device/generic/car/emulator/vipee_vhal"

rm -rf "$DST"
mkdir -p "$DST/include/vipee"
cp "$REPO"/android/vhal/{Android.bp,*.cpp,*.h,*.rc,*.json,*.xml} "$DST/"
cp "$REPO"/android/vhal/include/vipee/*.h "$REPO/core/include/vipee/wire.h" \
  "$DST/include/vipee/"
find "$DST" -type f | sort
