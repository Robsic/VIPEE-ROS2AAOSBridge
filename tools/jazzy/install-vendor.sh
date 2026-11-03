#!/usr/bin/env bash
# Instala o vipee_bridge como serviço vendor no emulador (AD-012), no lugar do
# spike vipee_twist_listener. Pré-requisitos: emulador iniciado com
# -writable-system (~/aosp-scripts/emu_rw.sh), build-android.sh e
# build-bridge.sh já rodados. Reinicia o emulador no fim. Se existir
# $OUT_VENDOR/bin/hw/vipee-vehicle-service (build do VHAL, T13), ele e o .rc
# substituem o VHAL do emulador; o original fica em *.stock e volta no remove.
# OUT_VENDOR aponta para o vendor de um build AOSP (local ou trazido da VM).
#   tools/jazzy/install-vendor.sh              # instala
#   tools/jazzy/install-vendor.sh remove       # remove tudo (inclui o EVS)
#   tools/jazzy/install-vendor.sh evs          # câmeras: v4l2loopback + evs-v4l2 (AD-015)
#   tools/jazzy/install-vendor.sh evs-remove   # devolve o EVS de fábrica (MP4)
# O passo evs troca a fonte do EVS do emulador compartilhado: avise AVM e
# CalibDroid antes. Precisa de vm_m.sh vipee-evs-v4l2 selinux_policy (AD-016).
set -euo pipefail

ADB="${ADB:-$HOME/aosp/out/host/linux-x86/bin/adb}"
WS="${WS:-$HOME/ros2_android_ws}"
HERE="$(cd "$(dirname "$0")" && pwd)"
MAPS="$HERE/../../maps"
OUT_VENDOR="${OUT_VENDOR:-$HOME/aosp/out/target/product/emulator_car64_x86_64/vendor}"
# Libs direto em /vendor/lib64: o exec com troca de domínio SELinux liga AT_SECURE
# e o linker ignora LD_LIBRARY_PATH (sintoma: serviço sai com 1 antes do main).
LIB=/vendor/lib64
LIBS="$(cd "$WS/push_x86_64/lib" && ls)"
VHAL_DEV=/vendor/bin/hw/android.hardware.automotive.vehicle@V4-emulator-service
VHAL_RC=/vendor/etc/init/vhal-emulator-service.rc
VHAL_BIN="$OUT_VENDOR/bin/hw/vipee-vehicle-service"
EVS_DIR="$HERE/../../android/evs"
EVS_HAL=/vendor/bin/hw/vipee-evs-v4l2
EVS_VINTF=/vendor/etc/vintf/manifest/manifest_vipee-evs-v4l2.xml
EVS_RC=/vendor/etc/init/init.evs.rc
EVS_XML=/vendor/etc/automotive/evs/evs_configuration_override.xml
UEVENTD=/vendor/etc/ueventd.rc
# RRO estático com os tipos do CarEvsService ➔ /dev/video10..13, acima do
# CarEvsServiceOverlay (mapeamento persistente; some com evs-remove).
EVS_RRO=/product/overlay/VipeeEvsCameraOverlay.apk
# Tipo de câmera do CarEvsService ➔ dispositivo da ponte. O mapeamento persistente
# vem do RRO (EVS_RRO); o set-camera-id depois do reboot do passo evs só confirma
# o mesmo valor em memória. tools/emu/test-cam.sh install confere.
EVS_IDS="FRONTVIEW:/dev/video10 REARVIEW:/dev/video11 LEFTVIEW:/dev/video12 RIGHTVIEW:/dev/video13"

evs_restore() {
  "$ADB" shell "for f in $EVS_RC $EVS_XML $UEVENTD; do [ -f \$f.stock ] && mv \$f.stock \$f; done; rm -f $EVS_HAL $EVS_VINTF $EVS_RRO /vendor/bin/hw/android.hardware.automotive.evs-v4l2 /vendor/etc/vintf/manifest/manifest_android.hardware.automotive.evs-v4l2.xml"
}

evs_install() {
  "$ADB" shell "for f in $EVS_RC $EVS_XML $UEVENTD; do [ -f \$f.stock ] || cp -p \$f \$f.stock; done"
  "$ADB" push "$OUT_VENDOR/bin/hw/vipee-evs-v4l2" "$EVS_HAL"
  # Rótulo hal_evs_default_exec do fork vem da política da ponte (file_contexts).
  "$ADB" push "$OUT_VENDOR/etc/selinux/." /vendor/etc/selinux/ | tail -1
  "$ADB" push "$OUT_VENDOR/etc/vintf/manifest/$(basename "$EVS_VINTF")" "$EVS_VINTF"
  "$ADB" push "$OUT_VENDOR/../product/overlay/VipeeEvsCameraOverlay.apk" "$EVS_RRO"
  "$ADB" push "$EVS_DIR/init.vipee_evs.rc" "$EVS_RC"
  "$ADB" push "$EVS_DIR/evs_configuration_v4l2.xml" "$EVS_XML"
  "$ADB" shell "cat $UEVENTD.stock > $UEVENTD"
  "$ADB" shell "cat >> $UEVENTD" < "$EVS_DIR/ueventd.vipee_evs.rc"
  "$ADB" shell "chmod 755 $EVS_HAL && restorecon $EVS_HAL $EVS_VINTF $EVS_RC $EVS_XML $UEVENTD && ls -Z $EVS_HAL"
}

evs_camera_ids() {
  for m in $EVS_IDS; do "$ADB" shell cmd car_service set-camera-id "${m%%:*}" "${m#*:}"; done
}

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
if ! "$ADB" remount 2>&1 | grep -qi 'remount succeeded'; then
  # Primeira vez: o remount liga o overlayfs e pede reboot.
  "$ADB" reboot; sleep 10; boot
  "$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
  "$ADB" remount
fi

if [ "${1:-}" = evs ]; then
  evs_install
elif [ "${1:-}" = evs-remove ]; then
  evs_restore
elif [ "${1:-}" = remove ]; then
  evs_restore
  "$ADB" shell "for f in $VHAL_DEV $VHAL_RC; do [ -f \$f.stock ] && mv \$f.stock \$f; done; restorecon $VHAL_DEV"
  "$ADB" shell "cd $LIB && rm -f $(echo $LIBS)"
  "$ADB" shell rm -rf /vendor/lib64/vipee_ros2 /vendor/bin/vipee_bridge /vendor/bin/vipee_twist_listener \
    /vendor/etc/vipee /vendor/etc/init/vipee_bridge.rc /vendor/etc/init/vipee_twist_listener.rc
else
  "$ADB" shell rm -rf /vendor/lib64/vipee_ros2
  "$ADB" shell rm -f /vendor/bin/vipee_twist_listener /vendor/etc/init/vipee_twist_listener.rc
  "$ADB" shell mkdir -p /vendor/etc/vipee/maps
  "$ADB" push "$WS/push_x86_64/lib/." "$LIB/" | tail -1
  "$ADB" push "$WS/bridge_build_x86_64/vipee_bridge" /vendor/bin/vipee_bridge
  "$ADB" push "$HERE/config/cyclonedds_device.xml" /vendor/etc/vipee/
  "$ADB" push "$MAPS/." /vendor/etc/vipee/maps/
  "$ADB" push "$HERE/vendor/vipee_bridge.rc" /vendor/etc/init/
  "$ADB" shell chmod 755 /vendor/bin/vipee_bridge
  # Política com o domínio vendor_vipee_ros2 (sepolicy-sync.sh + m selinux_policy).
  "$ADB" push "$OUT_VENDOR/etc/selinux/." /vendor/etc/selinux/ | tail -1
  if [ -f "$VHAL_BIN" ]; then
    "$ADB" shell "for f in $VHAL_DEV $VHAL_RC; do [ -f \$f.stock ] || cp -p \$f \$f.stock; done"
    "$ADB" push "$VHAL_BIN" "$VHAL_DEV"
    "$ADB" push "$HERE/../../android/vhal/vipee-vehicle-service.rc" "$VHAL_RC"
    "$ADB" push "$HERE/../../android/vhal/vipee_properties.json" /vendor/etc/vipee/
    # Rótulo hal_vehicle_default_exec vem do file_contexts do caminho original.
    "$ADB" shell "chmod 755 $VHAL_DEV && restorecon $VHAL_DEV && ls -Z $VHAL_DEV"
  fi
fi
"$ADB" reboot; sleep 10; boot
"$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
if [ "${1:-}" = evs ]; then
  evs_camera_ids
  "$ADB" shell ls -lZ /dev/video10 /dev/video11 /dev/video12 /dev/video13
  exit 0
elif [ "${1:-}" = evs-remove ]; then
  exit 0
elif [ "${1:-}" != remove ]; then
  # O tipo só existe depois do boot com a política nova; o overlay volta ro no boot.
  "$ADB" remount >/dev/null
  "$ADB" shell chcon u:object_r:vendor_vipee_ros2_exec:s0 /vendor/bin/vipee_bridge
  "$ADB" shell restorecon -R /vendor/etc/vipee
  "$ADB" shell stop vipee_bridge; "$ADB" shell start vipee_bridge
fi
"$ADB" shell ls -Z /vendor/bin/vipee_bridge 2>/dev/null || echo "removido"
