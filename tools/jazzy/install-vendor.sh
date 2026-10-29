#!/usr/bin/env bash
# Spike AD-012: instala o twist_listener como serviço vendor no emulador.
# Pré-requisitos: emulador iniciado com -writable-system (~/aosp-scripts/emu_rw.sh),
# build-android.sh e build-bringup.sh já rodados. Reinicia o emulador no fim.
#   tools/jazzy/install-vendor.sh          # instala
#   tools/jazzy/install-vendor.sh remove   # remove tudo
set -euo pipefail

ADB="${ADB:-$HOME/aosp/out/host/linux-x86/bin/adb}"
WS="${WS:-$HOME/ros2_android_ws}"
HERE="$(cd "$(dirname "$0")" && pwd)"
# Libs direto em /vendor/lib64: o exec com troca de domínio SELinux liga AT_SECURE
# e o linker ignora LD_LIBRARY_PATH (sintoma: serviço sai com 1 antes do main).
LIB=/vendor/lib64
LIBS="$(cd "$WS/push_x86_64/lib" && ls)"

boot() {
  "$ADB" wait-for-device
  for _ in $(seq 120); do
    [ "$("$ADB" shell getprop sys.boot_completed | tr -d '\r')" = 1 ] && return 0
    sleep 5
  done
  return 1
}

boot
"$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
if ! "$ADB" remount 2>&1 | grep -q 'remount succeeded'; then
  # Primeira vez: o remount liga o overlayfs e pede reboot.
  "$ADB" reboot; sleep 10; boot
  "$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
  "$ADB" remount
fi

if [ "${1:-}" = remove ]; then
  "$ADB" shell "cd $LIB && rm -f $(echo $LIBS)"
  "$ADB" shell rm -rf /vendor/lib64/vipee_ros2 /vendor/bin/vipee_twist_listener /vendor/etc/vipee \
    /vendor/etc/init/vipee_twist_listener.rc
else
  "$ADB" shell rm -rf /vendor/lib64/vipee_ros2
  "$ADB" shell mkdir -p /vendor/etc/vipee
  "$ADB" push "$WS/push_x86_64/lib/." "$LIB/" | tail -1
  "$ADB" push "$WS/bringup_build_x86_64/twist_listener" /vendor/bin/vipee_twist_listener
  "$ADB" push "$HERE/config/cyclonedds_device.xml" /vendor/etc/vipee/
  "$ADB" push "$HERE/vendor/vipee_twist_listener.rc" /vendor/etc/init/
  "$ADB" shell chmod 755 /vendor/bin/vipee_twist_listener
  "$ADB" shell chcon u:object_r:hal_vehicle_default_exec:s0 /vendor/bin/vipee_twist_listener
fi
"$ADB" reboot; sleep 10; boot
"$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
"$ADB" shell ls -Z /vendor/bin/vipee_twist_listener 2>/dev/null || echo "removido"
