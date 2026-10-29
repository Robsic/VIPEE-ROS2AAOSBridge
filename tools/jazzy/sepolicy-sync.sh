#!/usr/bin/env bash
# Copia a política SELinux da ponte (tools/jazzy/sepolicy) para a árvore AOSP e
# liga o diretório no produto do emulador. Escreve em $ANDROID_BUILD_TOP: rodar
# só com aprovação (AGENTS.md §5). Depois: ~/aosp-scripts/m_module.sh selinux_policy
set -euo pipefail

AOSP="${ANDROID_BUILD_TOP:-$HOME/aosp}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DST=device/generic/car/emulator/vipee_ros2/sepolicy
MK="$AOSP/device/generic/car/emulator/car_emulator_vendor.mk"

mkdir -p "$AOSP/$DST"
cp "$HERE"/sepolicy/* "$AOSP/$DST/"
grep -qF "$DST" "$MK" || printf '\n# VIPEE-ROS2AAOSBridge (tools/jazzy/sepolicy-sync.sh)\nBOARD_SEPOLICY_DIRS += %s\n' "$DST" >> "$MK"
ls "$AOSP/$DST"
grep -n "$DST" "$MK"
