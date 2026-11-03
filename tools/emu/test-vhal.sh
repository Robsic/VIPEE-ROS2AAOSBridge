#!/usr/bin/env bash
# Gate de integração do VHAL da ponte no emulador (T13–T16). Pré-requisito:
# tools/jazzy/install-vendor.sh com o vipee-vehicle-service. Etapas:
#   absent  - sem o vipee_bridge: CarService no ar e as 4 UNAVAILABLE (T13)
#   live    - telemetria fake do host: valores lidos; SIGSTOP no bridge leva às
#             4 UNAVAILABLE pelo watchdog (≤ 300 ms); SIGCONT volta (T14)
#   rate    - telemetria a ~66 Hz gera pelo menos a taxa assinada de eventos
#             (T15; contrato do IVehicleHardware com VUR desligado)
#   trigger - gatilho do CalibDroid 0x21402a11, local ao VHAL (AD-018):
#             AVAILABLE 0 no boot, set grava, ponte muda não o derruba
#   tools/emu/test-vhal.sh [absent|live|rate|trigger]...   # padrão: todas
# Sai com 1 na primeira falha. Avc dos dois domínios também falha (T16).
set -euo pipefail

ADB="${ADB:-$HOME/aosp/out/host/linux-x86/bin/adb}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="${WS:-$HOME/ros2_android_ws}"  # install_sirv de test-ros.sh
VHAL=android.hardware.automotive.vehicle.IVehicle/default
SPEED=0x11600207 GEAR_SEL=0x11400400 CUR_GEAR=0x11400401 BRAKE=0x11200402
TRIGGER=0x21402a11  # CalibDroid AD-010, local ao VHAL (AD-018)
PUB=vipee_test_pub

fail() { echo "FAIL: $*" >&2; exit 1; }
sh() { "$ADB" shell "$@" | tr -d '\r'; }
get() { sh cmd car_service get-property-value "$1" 2>&1; }
# AD-017: sem dado o get é OK com status UNAVAILABLE e valor zero do formato,
# nunca o último valor real. Saída do shell (HalPropValue.toString):
# "SystemStatus: UNAVAILABLE(0x1), VendorStatus: 0, Value: ...".
unavail_re() {
  case $1 in
    "$SPEED") echo 'SystemStatus: UNAVAILABLE.*Value: 0\.0 METER_PER_SEC' ;;
    "$BRAKE") echo 'SystemStatus: UNAVAILABLE.*Value: FALSE' ;;
    *) echo 'SystemStatus: UNAVAILABLE.*Value: GEAR_UNKNOWN\(0x0\)' ;;
  esac
}
unavailable() { get "$1" | grep -qE "$(unavail_re "$1")"; }

boot() {
  "$ADB" wait-for-device
  for _ in $(seq 120); do
    [ "$(sh getprop sys.boot_completed)" = 1 ] && break
    sleep 5
  done
  "$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
  [ "$(sh getprop init.svc.vendor.vehicle-hal-emulator)" = running ] ||
    fail "VHAL não está rodando"
  for _ in $(seq 30); do
    sh service check car_service | grep -q ': found' && return 0
    sleep 2
  done
  fail "car_service não registrou (VHAL sem propriedades exigidas? ver AD-014)"
}

avc_check() {
  if sh logcat -d -b all | grep 'avc: *denied' |
      grep -E 'scontext=u:r:(vendor_vipee_ros2|hal_vehicle_default):'; then
    fail "negações SELinux dos domínios da ponte"
  fi
}

stop_pub() { docker rm -f "$PUB" >/dev/null 2>&1 || true; }

# fake_sim.py (um só participante DDS: o host só escuta a porta TCP 7400).
# Estado: 36 km/h, marcha 2, alavanca D, freio de mão solto.
STATE_DIR="$(mktemp -d)"
start_pub() {
  # Reaproveita o publicador vivo: reiniciar custa ~8 s de descoberta DDS.
  docker ps -q -f name="^$PUB\$" | grep -q . && return 0
  echo '{"speed": 36.0, "gear": 2, "lever": 68, "handbrake": false}' \
    > "$STATE_DIR/state.json"
  docker run -d --rm --name "$PUB" --network host \
    -v "$HERE/../jazzy/config:/cfg:ro" -v "$WS:/ws:ro" -v "$HERE:/emu:ro" \
    -v "$STATE_DIR:/state:ro" \
    -e CYCLONEDDS_URI=file:///cfg/cyclonedds_host.xml -e ROS_DOMAIN_ID=25 \
    -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
    vipee/ros2-jazzy-android bash -c '
      source /opt/ros/jazzy/setup.bash
      source /ws/ros_test/install_sirv/setup.bash
      exec python3 /emu/fake_sim.py cat793f /state/state.json' >/dev/null
}

wait_speed() {
  for _ in $(seq 30); do
    get $SPEED | grep -q '10\.0' && return 0
    sleep 1
  done
  fail "velocidade != 10.0 m/s: $(get $SPEED)"
}

absent() {
  sh stop vipee_bridge
  sleep 1
  for p in $SPEED $GEAR_SEL $CUR_GEAR $BRAKE; do
    unavailable "$p" || fail "absent: $p não é UNAVAILABLE com zero: $(get "$p")"
  done
  echo "absent: OK"
}

live() {
  sh start vipee_bridge
  start_pub
  trap stop_pub EXIT
  wait_speed
  get $CUR_GEAR | grep -qE '\b32\b|0x20' || fail "live: CURRENT_GEAR != GEAR_2: $(get $CUR_GEAR)"
  get $GEAR_SEL | grep -qE '\b8\b|0x8' || fail "live: GEAR_SELECTION != DRIVE: $(get $GEAR_SEL)"
  get $BRAKE | grep -qE 'SystemStatus: AVAILABLE.*Value: FALSE' || fail "live: PARKING_BRAKE_ON != 0: $(get $BRAKE)"

  # Watchdog: o init vê o processo vivo, mas parado; o VHAL tem de largar.
  sh logcat -c
  sh "log -t vipee_test STOP; kill -STOP \$(pidof vipee_bridge)"
  sleep 1
  local t0 t1
  t0=$(sh logcat -d -v epoch -s vipee_test | awk '/STOP/{print $1}')
  t1=$(sh logcat -d -v epoch -s VipeeVehicleHardware | awk '/silent/{print $1; exit}')
  sh "kill -CONT \$(pidof vipee_bridge)"
  [ -n "$t1" ] || fail "live: watchdog não disparou"
  # 300 ms do watchdog + 50 ms de poll + 100 ms do último heartbeat.
  awk -v a="$t0" -v b="$t1" 'BEGIN{exit !((b-a) <= 0.45)}' ||
    fail "live: watchdog levou $(awk -v a="$t0" -v b="$t1" 'BEGIN{print b-a}') s"
  for _ in $(seq 10); do
    get $GEAR_SEL | grep -qE '\b8\b|0x8' && { echo "live: OK"; return 0; }
    sleep 0.5
  done
  fail "live: sem resync depois do SIGCONT: $(get $GEAR_SEL)"
}

rate() {
  sh start vipee_bridge
  start_pub
  trap stop_pub EXIT
  wait_speed  # descoberta DDS leva ~8 s; medir antes pega o fluxo pela metade
  sleep 1
  local n0 n1 hz
  hz=$(sh dumpsys $VHAL | awk -F': ' '/speed rate Hz/{print $2}')
  awk -v h="$hz" 'BEGIN{exit !(h >= 1 && h <= 10)}' ||
    fail "rate: assinatura da velocidade a ${hz:-?} Hz, fora de 1–10"
  n0=$(sh dumpsys $VHAL | awk -F': ' '/speed events/{print $2}')
  sleep 5
  n1=$(sh dumpsys $VHAL | awk -F': ' '/speed events/{print $2}')
  # IVehicleHardware.h, VUR desligado: pelo menos a taxa assinada. O VHAL
  # repassa toda amostra (~66 Hz) e o cliente do CarService dizima.
  awk -v n=$((n1 - n0)) -v h="$hz" 'BEGIN{exit !(n >= 5*h)}' ||
    fail "rate: $((n1 - n0)) eventos em 5 s, abaixo de $hz Hz"
  echo "rate: OK ($((n1 - n0)) eventos em 5 s, assinatura a $hz Hz)"
}

# Valor int32 do vendor sai cru no shell: "SystemStatus: AVAILABLE(0x0), ...,
# Value: 1". Sem "set" no boot, o primeiro get tem de ver o 0 da config.
trigger_is() {
  get $TRIGGER | grep -qE "SystemStatus: AVAILABLE.*Value: $1(\$|[^0-9x])"
}

trigger() {
  sh stop vipee_bridge  # a ponte muda não afeta a propriedade local
  sleep 1               # > 300 ms do watchdog
  trigger_is 0 || fail "trigger: não é AVAILABLE 0 com a ponte muda: $(get $TRIGGER)"
  # CarShellCommand: set-property-value <prop> <areaId> <data>; erro sai com 0.
  sh cmd car_service set-property-value $TRIGGER 0 1 | grep -q successfully ||
    fail "trigger: set recusado"
  trigger_is 1 || fail "trigger: get depois do set != 1: $(get $TRIGGER)"
  sleep 1
  trigger_is 1 || fail "trigger: caiu com a ponte muda: $(get $TRIGGER)"
  sh cmd car_service set-property-value $TRIGGER 0 0 >/dev/null
  echo "trigger: OK"
}

boot
for step in "${@:-absent live rate trigger}"; do
  for s in $step; do "$s"; done
done
avc_check
echo "test-vhal: OK"
