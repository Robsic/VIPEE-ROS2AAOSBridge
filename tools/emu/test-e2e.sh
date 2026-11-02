#!/usr/bin/env bash
# Teste ponta a ponta da ct-speed-slice (T17): fake_sim.py no host ➔ vipee_bridge
# ➔ VHAL ➔ CarService no emulador. Os 3 Success Criteria do spec:
#   1. 10 velocidades seguidas legíveis em ≤ 200 ms após a publicação
#   2. telemetria parada ➔ UNAVAILABLE em ≤ 600 ms e STALE no logcat
#   3. alavanca 'P' dá GEAR_PARK com o mapa do CAT793F; com o do Commander a
#      mesma telemetria dá a seleção derivada (DRIVE), sem recompilar
# Pré-requisito: install-vendor.sh com o VHAL; Wi-Fi do emulador ligado.
# A latência medida no host inclui o adb (limite superior da real).
set -euo pipefail

ADB="${ADB:-$HOME/aosp/out/host/linux-x86/bin/adb}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="${WS:-$HOME/ros2_android_ws}"
VHAL=android.hardware.automotive.vehicle.IVehicle/default
SPEED=0x11600207 GEAR_SEL=0x11400400
SIM=vipee_e2e_sim
STATE_DIR="$(mktemp -d)"

fail() { echo "FAIL: $*" >&2; exit 1; }
sh() { "$ADB" shell "$@" | tr -d '\r'; }
get() { sh cmd car_service get-property-value "$1" 2>&1; }
ms() { date +%s%3N; }
state() {  # escrita atômica: o fake_sim relê pelo mtime
  echo "$1" > "$STATE_DIR/s.tmp" && mv "$STATE_DIR/s.tmp" "$STATE_DIR/state.json"
}
stop_sim() { docker rm -f "$SIM" >/dev/null 2>&1 || true; }
trap stop_sim EXIT

start_sim() {  # $1: namespace do veículo
  stop_sim
  docker run -d --rm --name "$SIM" --network host \
    -v "$HERE/../jazzy/config:/cfg:ro" -v "$WS:/ws:ro" -v "$HERE:/emu:ro" \
    -v "$STATE_DIR:/state:ro" \
    -e CYCLONEDDS_URI=file:///cfg/cyclonedds_host.xml -e ROS_DOMAIN_ID=25 \
    -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
    vipee/ros2-jazzy-android bash -c "
      source /opt/ros/jazzy/setup.bash
      source /ws/ros_test/install_sirv/setup.bash
      exec python3 /emu/fake_sim.py $1 /state/state.json" >/dev/null
}

# Espera, no device, até o get casar o padrão; devolve o tempo no host (ms).
until_get() {  # $1 prop, $2 regex, $3 limite em segundos
  local t0 t1
  t0=$(ms)
  sh "end=\$((\$(date +%s) + $3))
      until cmd car_service get-property-value $1 2>&1 | grep -qE '$2'; do
        [ \$(date +%s) -gt \$end ] && exit 1; done; :" || return 1
  t1=$(ms)
  echo $((t1 - t0))
}

vehicle() {  # $1: cat793f | commander
  sh setprop vendor.vipee.vehicle "$1"
  sh "stop vipee_bridge; start vipee_bridge"
}

latency() {
  local worst=0 t k
  # 2..20 m/s (7,2..72 km/h, dentro do max_abs_kmh 80 do CAT793F); o float
  # pode chegar como 5.9999995 em vez de 6.0.
  for k in 1 2 3 4 5 6 7 8 9 10; do
    local v=$((2 * k))
    state "{\"speed\": $(awk -v v=$v 'BEGIN{print v * 3.6}'), \"gear\": 2, \"lever\": 68}"
    t=$(until_get $SPEED "Value: ($v\.0|$((v - 1))\.99)[0-9]* METER_PER_SEC" 5) ||
      fail "latência: $v m/s não apareceu: $(get $SPEED)"
    echo "  $v m/s em $t ms"
    [ "$t" -gt "$worst" ] && worst=$t
  done
  [ "$worst" -le 200 ] || fail "latência: pior caso $worst ms > 200 ms"
  echo "1. latência: OK (pior $worst ms, com adb)"
}

stale() {
  sh logcat -c
  state '{"speed": 36.0, "gear": 2, "lever": 68, "telemetry": false}'
  local t
  t=$(until_get $SPEED 'Can not get property value' 5) ||
    fail "dado velho: velocidade não ficou indisponível"
  sh logcat -d -s vipee_bridge | grep -q '0x11600207 UNAVAILABLE STALE' ||
    fail "dado velho: sem STALE no logcat"
  [ "$t" -le 600 ] || fail "dado velho: $t ms > 600 ms"
  echo "2. dado velho: OK ($t ms, STALE no logcat)"
}

maps() {
  # 'P' = 80 na alavanca; telemetria em marcha 2. GEAR_PARK 0x4, DRIVE 0x8.
  state '{"speed": 0.0, "gear": 2, "lever": 80}'
  until_get $GEAR_SEL 'Value: GEAR_PARK.0x4.' 5 >/dev/null ||
    fail "CAT793F: 'P' não deu GEAR_PARK: $(get $GEAR_SEL)"
  start_sim commander  # antes do bridge: o device disca o host só na partida
  sleep 3              # e depois a cada ~30 s
  vehicle commander
  until_get $GEAR_SEL 'Value: GEAR_DRIVE.0x8.' 30 >/dev/null ||
    fail "Commander: seleção derivada não deu DRIVE: $(get $GEAR_SEL)"
  echo "3. mapas: OK (CAT793F 'P' ➔ GEAR_PARK; Commander marcha 2 ➔ DRIVE)"
}

[ "$(sh cmd wifi status | head -1)" = "Wifi is enabled" ] ||
  fail "Wi-Fi do emulador desligado: adb shell cmd wifi set-wifi-enabled enabled"
state '{"speed": 0.0, "gear": 2, "lever": 68}'
start_sim cat793f
sleep 3  # contêiner escutando a 7400 antes da partida do bridge
vehicle cat793f
t=$(until_get $SPEED 'Value: 0\.0 METER_PER_SEC' 90) && echo "dados em $t ms" ||
  fail "sem dados do fake_sim: $(get $SPEED)"
latency
stale
state '{"speed": 0.0, "gear": 2, "lever": 68}'
maps
vehicle cat793f
sh logcat -d -b all | grep 'avc: *denied' |
  grep -E 'scontext=u:r:(vendor_vipee_ros2|hal_vehicle_default):' &&
  fail "negações SELinux"
echo "test-e2e: OK"
