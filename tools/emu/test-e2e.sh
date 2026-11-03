#!/usr/bin/env bash
# Teste ponta a ponta da ct-speed-slice (T17): fake_sim.py no host ➔ vipee_bridge
# ➔ VHAL ➔ CarService no emulador. Os 3 Success Criteria do spec:
#   1. 10 leituras seguidas das 4 propriedades em ≤ 200 ms após a publicação
#   2. simulador mudo ➔ as 4 UNAVAILABLE em ≤ 600 ms e STALE no logcat
#   3. alavanca 'P' dá GEAR_PARK com o mapa do CAT793F; com o do Commander a
#      mesma telemetria dá a seleção derivada (DRIVE), sem recompilar
# Com as 4 câmeras do fake_sim ligadas o tempo todo (cam-evs-slice, CAM-05,
# CAM-08): os critérios valem com vídeo, e no fim o STATS das 4 tem quadros.
# Pré-requisito: install-vendor.sh com o VHAL; Wi-Fi do emulador ligado.
# A latência medida no host inclui o adb (limite superior da real).
set -euo pipefail

ADB="${ADB:-$HOME/aosp/out/host/linux-x86/bin/adb}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="${WS:-$HOME/ros2_android_ws}"
VHAL=android.hardware.automotive.vehicle.IVehicle/default
SPEED=0x11600207 GEAR_SEL=0x11400400 CUR_GEAR=0x11400401 BRAKE=0x11200402
SIM=vipee_e2e_sim
# Status no get (HalPropValue.toString); UNAVAILABLE também é get OK (AD-017).
OK='SystemStatus: AVAILABLE' NA='SystemStatus: UNAVAILABLE'
STATE_DIR="$(mktemp -d)"
BAG="${BAG:-$HOME/rosbags/rosbag2_calibration_2026_10_01}"
POS="front rear left right"
CAMS='"cameras": {"front": "ok", "rear": "ok", "left": "ok", "right": "ok"}'

fail() { echo "FAIL: $*" >&2; exit 1; }
sh() { "$ADB" shell "$@" | tr -d '\r'; }
get() { sh cmd car_service get-property-value "$1" 2>&1; }
ms() { date +%s%3N; }
state() {  # escrita atômica: o fake_sim relê pelo mtime; câmeras sempre ok
  echo "${1%\}}, $CAMS}" > "$STATE_DIR/s.tmp" && mv "$STATE_DIR/s.tmp" "$STATE_DIR/state.json"
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
      exec python3 /emu/fake_sim.py $1 /state/state.json /state" >/dev/null
}

# Um JPEG 1280x720 por câmera, tirado do bag de calibração, para o fake_sim.
frames() {
  docker run --rm --entrypoint '' --user "$(id -u):$(id -g)" \
    -v "$BAG:/bag:ro" -v "$HERE:/emu:ro" -v "$STATE_DIR:/out" \
    vipee/ros2-jazzy-android bash -c \
    "source /opt/ros/jazzy/setup.bash && python3 /emu/extract_frames.py /bag /out" >/dev/null
}

cam_stats() {
  # As 4 câmeras entregaram quadros: STATS com frames > 0 de cada uma.
  local p end=$(($(date +%s) + 15))
  for p in $POS; do
    until sh logcat -d -s vipee_bridge | grep -q " CAM $p STATS frames=[1-9]"; do
      [ "$(date +%s)" -gt $end ] && fail "câmeras: sem STATS com quadros de $p"
      sleep 1
    done
  done
  sh logcat -d -s vipee_bridge | grep ' STATS ' | tail -4
  echo "4. câmeras: OK (STATS com quadros das 4)"
}

# Espera, no device, até o get casar o padrão; devolve o tempo no host (ms).
until_get() {  # $1 prop, $2 regex, $3 limite em segundos
  until_all "$3" "$1" "$2"
}

# Espera, no device, até todos os pares (prop, regex) casarem; devolve o
# tempo no host (ms). $1 limite em segundos, depois pares prop regex.
until_all() {
  local t0 t1 limit=$1 cond=""
  shift
  while [ $# -gt 0 ]; do
    cond="$cond && cmd car_service get-property-value $1 2>&1 | grep -qE '$2'"
    shift 2
  done
  t0=$(ms)
  sh "end=\$((\$(date +%s) + $limit))
      until true $cond; do
        [ \$(date +%s) -gt \$end ] && exit 1; done; :" || return 1
  t1=$(ms)
  echo $((t1 - t0))
}

vehicle() {  # $1: cat793f | commander
  sh setprop vendor.vipee.vehicle "$1"
  sh "stop vipee_bridge; start vipee_bridge"
}

latency() {
  # As 4 propriedades mudam juntas a cada leitura: velocidade 2..20 m/s
  # (7,2..72 km/h, dentro do max_abs_kmh 80 do CAT793F; o float pode chegar
  # como 5.9999995), marcha 1..9 ➔ GEAR_k, alavanca D/N, freio alternado.
  local worst=0 t k v g lever sel brake
  for k in 1 2 3 4 5 6 7 8 9 10; do
    v=$((2 * k)) g=$(((k - 1) % 9 + 1))
    if [ $((k % 2)) = 1 ]; then lever=68 sel='GEAR_DRIVE.0x8.' brake=true
    else lever=78 sel='GEAR_NEUTRAL.0x1.' brake=false; fi
    state "{\"speed\": $(awk -v v=$v 'BEGIN{print v * 3.6}'), \"gear\": $g, \"lever\": $lever, \"handbrake\": $brake}"
    t=$(until_all 5 \
      $SPEED "$OK.*Value: ($v\\.0|$((v - 1))\\.99)[0-9]* METER_PER_SEC" \
      $CUR_GEAR "$OK.*Value: GEAR_$g\\(" \
      $GEAR_SEL "$OK.*Value: $sel" \
      $BRAKE "$OK.*Value: $(echo $brake | tr a-z A-Z)") ||
      fail "latência: leitura $k não apareceu: $(get $SPEED); $(get $CUR_GEAR); $(get $GEAR_SEL); $(get $BRAKE)"
    echo "  $v m/s, GEAR_$g, alavanca $lever, freio $brake em $t ms"
    [ "$t" -gt "$worst" ] && worst=$t
  done
  [ "$worst" -le 200 ] || fail "latência: pior caso $worst ms > 200 ms"
  echo "1. latência: OK (pior $worst ms, 4 propriedades, com adb)"
}

stale() {
  # Simulador mudo: as 4 propriedades ficam indisponíveis.
  sh logcat -c
  state '{"speed": 7.2, "gear": 2, "lever": 68, "publish": false}'
  local t p
  # AD-017: get OK com status UNAVAILABLE e valor zero, nunca o último real.
  t=$(until_all 5 $SPEED "$NA.*Value: 0\\.0 METER_PER_SEC" \
    $CUR_GEAR "$NA.*Value: GEAR_UNKNOWN\\(0x0\\)" \
    $GEAR_SEL "$NA.*Value: GEAR_UNKNOWN\\(0x0\\)" $BRAKE "$NA.*Value: FALSE") ||
    fail "dado velho: alguma propriedade não ficou indisponível"
  for p in $SPEED $CUR_GEAR $GEAR_SEL $BRAKE; do
    sh logcat -d -s vipee_bridge | grep -q "0x${p#0x} UNAVAILABLE STALE" ||
      fail "dado velho: sem STALE de $p no logcat"
  done
  [ "$t" -le 600 ] || fail "dado velho: $t ms > 600 ms"
  echo "2. dado velho: OK ($t ms, 4 propriedades, STALE no logcat)"
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
frames
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
cam_stats
sh logcat -d -b all | grep 'avc: *denied' |
  grep -E 'scontext=u:r:(vendor_vipee_ros2|hal_vehicle_default):' &&
  fail "negações SELinux"
echo "test-e2e: OK"
