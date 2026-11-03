#!/usr/bin/env bash
# Gate de integração das câmeras no emulador (cam-evs-slice). Pré-requisito:
# tools/jazzy/install-vendor.sh evs (v4l2loopback + evs-v4l2) e o probe
# compilado (vm_m.sh vipee_evs_probe). Etapas:
#   platform - escritor do spike em 1280x720 YUYV nos /dev/video10..13: o probe
#              lista os 4 e recebe quadros 1280x720 a >= 8 Hz de cada; sem avc
#              de vendor_modprobe, hal_evs_default ou vendor_vipee_ros2
#              (CAM-19, CAM-21).
#   stall    - HAL com escritor parado (AD-016): câmera aberta sem escritor
#              abre, não recebe quadro e fecha em < 1 s; escritor que chega
#              depois entrega 1280x720; escritor que para com o fd aberto não
#              repete quadro e a câmera fecha em < 1 s; escritor que volta
#              retoma sem reabrir (CAM-04, CAM-06, CAM-07)
# Os passos abaixo usam a ponte instalada (install-vendor.sh) e uma fonte DDS
# no host (bag ou fake_sim, nunca os dois: um participante só, AD-011):
#   bag      - bag de calibração em loop com vendor.vipee.vehicle=commander: o
#              probe recebe das 4 câmeras >= 8 quadros/s em 1280x720 por
#              BAG_S s (padrão 60) e todo STATS do período tem p95_ms <= 100
#              (CAM-01, CAM-02, CAM-03)
#   stale    - bag pausado: 600 ms depois nenhum quadro nas 4 e STALE das 4
#              no logcat; bag volta com a câmera aberta: quadros e LIVE das 4,
#              sem reabrir (CAM-04, CAM-06, CAM-07)
#   drops    - fake_sim: 480x480, PNG e JPEG cortado ➔ DROP com o motivo certo
#              uma vez por mudança, STALE e nenhum quadro nessas câmeras; a
#              quarta segue >= 8 quadros/s (CAM-08..13)
#   device   - mapa com a câmera front em /dev/video99: DEVICE_UNAVAILABLE uma
#              vez, o processo segue, as outras 3 >= 8 quadros/s e o VHAL
#              entrega a velocidade do fake_sim (CAM-14, CAM-15)
#   install  - evs-remove devolve os MP4; evs reinstala,
#              o probe volta a listar /dev/video10..13 e o CarEvsService usa
#              /dev/video10..13 para FRONT/REAR/LEFT/RIGHTVIEW (CAM-19,
#              CAM-20; dois reboots)
#   tools/emu/test-cam.sh [passo]...   # padrão: todos, na ordem acima
# Sai com 1 na primeira falha.
set -euo pipefail

ADB="${ADB:-$HOME/aosp/out/host/linux-x86/bin/adb}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT_VENDOR="${OUT_VENDOR:-$HOME/aosp/out/target/product/emulator_car64_x86_64/vendor}"
SPIKE="${SPIKE:-$HOME/ros2_android_ws/spike_v4l2/v4l2_spike}"
# Escritor que escreve e depois fica parado com o fd aberto (ponte em STALE).
PRIME="${PRIME:-$HOME/ros2_android_ws/spike_v4l2/prime}"
SILENT=/data/local/tmp/v4l2_prime
PROBE=/vendor/bin/vipee_evs_probe
WRITER=/data/local/tmp/v4l2_spike
DEVS="/dev/video10 /dev/video11 /dev/video12 /dev/video13"
W=1280 H=720
WS="${WS:-$HOME/ros2_android_ws}"
BAG="${BAG:-$HOME/rosbags/rosbag2_calibration_2026_10_01}"
BAG_S="${BAG_S:-60}"
SRC=vipee_cam_src  # contêiner da fonte DDS (bag ou fake_sim)
STATE_DIR="$(mktemp -d)"
POS="front rear left right"

fail() { echo "FAIL: $*" >&2; exit 1; }
sh() { "$ADB" shell "$@" | tr -d '\r'; }
stop_src() { docker rm -f "$SRC" >/dev/null 2>&1 || true; }
trap 'stop_src; rm -rf "$STATE_DIR"' EXIT

boot() {
  "$ADB" wait-for-device
  for _ in $(seq 120); do
    [ "$(sh getprop sys.boot_completed)" = 1 ] && break
    sleep 5
  done
  "$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
}

setup() {
  boot
  "$ADB" remount >/dev/null 2>&1 || fail "adb remount (emulador com -writable-system?)"
  "$ADB" push "$OUT_VENDOR/bin/vipee_evs_probe" "$PROBE" >/dev/null
  "$ADB" push "$SPIKE" "$WRITER" >/dev/null
  "$ADB" push "$PRIME" "$SILENT" >/dev/null
  sh chmod 755 "$PROBE" "$WRITER" "$SILENT"
  sh restorecon "$PROBE"
}

avc_check() {
  if sh logcat -d -b all | grep 'avc: *denied' |
      grep -E 'scontext=u:r:(vendor_modprobe|hal_evs_default|vendor_vipee_ros2):'; then
    fail "negações SELinux da plataforma de câmeras"
  fi
}

# Campo <k> da linha PROBE de <id> na saída <out>.
field() { echo "$1" | awk -v id="$2" -v k="$3" '$1 == "PROBE" && $2 == id {
  for (i = 3; i <= NF; i++) { split($i, kv, "="); if (kv[1] == k) print kv[2] } }'; }

writers_start() {  # <segundos>
  for d in $DEVS; do
    sh "nohup $WRITER write $d $W $H 10 $1 >/dev/null 2>&1 &"
  done
}
# Roda <cmd> no device em background, sobrevivendo ao fim do adb shell.
bg() { sh "nohup sh -c '$1' >/dev/null 2>&1 &"; }
writers_stop() { sh "pkill -x v4l2_spike; pkill -x v4l2_prime" || true; }

# platform e stall usam o escritor do spike: a ponte, se instalada, larga os
# dispositivos (o v4l2loopback aceita um escritor só) e volta no fim.
bridge_off() { sh stop vipee_bridge >/dev/null 2>&1 || true; }
bridge_on() { sh start vipee_bridge >/dev/null 2>&1 || true; }

step_platform() {
  echo "== platform"
  bridge_off
  for d in $DEVS; do
    sh ls -l "$d" | grep -q '^crw-rw---- 1 system camera ' || fail "$d sem system:camera 0660"
  done
  for s in vendor.evs-v4l2 evs_manager_cf; do
    [ "$(sh getprop init.svc.$s)" = running ] || fail "$s não está rodando"
  done
  local list
  list="$(sh $PROBE --list)"
  for d in $DEVS; do
    echo "$list" | grep -qx "CAMERA $d" || fail "probe não lista $d: $list"
  done
  writers_start 12
  sleep 1
  local out
  out="$(sh $PROBE 6 $DEVS)" || { writers_stop; fail "probe saiu com erro: $out"; }
  writers_stop
  echo "$out"
  for d in $DEVS; do
    local n w h
    n="$(field "$out" "$d" frames)"; w="$(field "$out" "$d" width)"; h="$(field "$out" "$d" height)"
    [ -n "$n" ] || fail "$d sem linha PROBE"
    [ "$n" -ge 48 ] || fail "$d: $n quadros em 6 s (< 8 Hz)"
    [ "$w" = "$W" ] && [ "$h" = "$H" ] || fail "$d: ${w}x${h}, esperado ${W}x${H}"
  done
  avc_check
  bridge_on
  echo "platform OK"
}

# Confere a linha PROBE de <id>: frames >= <min>, frames <= <max>, stop_ms < 1000.
check_probe() {  # <out> <id> <min> <max> <rótulo>
  local n st
  n="$(field "$1" "$2" frames)"; st="$(field "$1" "$2" stop_ms)"
  [ -n "$n" ] || fail "$5: sem linha PROBE ($1)"
  [ "$n" -ge "$3" ] && [ "$n" -le "$4" ] || fail "$5: $n quadros, esperado $3..$4"
  [ "$st" -lt 1000 ] || fail "$5: fechar levou $st ms (>= 1000)"
}

step_stall() {
  echo "== stall"
  local d=/dev/video10 out
  bridge_off
  writers_stop
  # 1. Sem escritor: abre, nenhum quadro, fecha rápido.
  out="$(sh $PROBE --close-after-ms 2000 3 $d)" || fail "sem escritor: probe falhou: $out"
  echo "$out"; check_probe "$out" $d 0 0 "sem escritor"
  # 2. Escritor chega 1 s depois da abertura: quadros 1280x720.
  bg "sleep 1; $WRITER write $d $W $H 10 3"
  out="$(sh $PROBE 5 $d)" || fail "escritor tardio: probe falhou: $out"
  echo "$out"; check_probe "$out" $d 20 40 "escritor tardio"
  [ "$(field "$out" $d width)x$(field "$out" $d height)" = "${W}x${H}" ] ||
    fail "escritor tardio: tamanho errado"
  writers_stop
  # 3. Escritor escreve 2 s e fica parado com o fd aberto; fecha no silêncio.
  bg "$SILENT burst $d 2 6"
  out="$(sh $PROBE --close-after-ms 4500 5 $d)" || fail "escritor parado: probe falhou: $out"
  echo "$out"; check_probe "$out" $d 12 25 "escritor parado"
  [ "$(field "$out" $d last_frame_ms_ago)" -ge 2000 ] || fail "escritor parado: quadro repetido"
  writers_stop
  # 4. Escritor some e volta: retoma sem reabrir.
  bg "$SILENT burst $d 2 1; sleep 1; $WRITER write $d $W $H 10 3"
  out="$(sh $PROBE 8 $d)" || fail "escritor volta: probe falhou: $out"
  echo "$out"; check_probe "$out" $d 35 60 "escritor volta"
  [ "$(field "$out" $d max_gap_ms)" -ge 1500 ] || fail "escritor volta: sem buraco (quadro inventado?)"
  [ "$(field "$out" $d last_frame_ms_ago)" -lt 2500 ] || fail "escritor volta: não retomou"
  writers_stop
  avc_check
  bridge_on
  echo "stall OK"
}

# Fonte DDS no contêiner, rede do host (CycloneDDS TCP na 7400, como o
# test-e2e.sh). O device só disca o host na partida da ponte e depois a cada
# ~30 s: a ponte reinicia depois que a fonte sobe.
src_run() {  # <comando no contêiner>
  stop_src
  docker run -d --rm --name "$SRC" --network host --entrypoint '' \
    -v "$HERE/../jazzy/config:/cfg:ro" -v "$WS:/ws:ro" -v "$HERE:/emu:ro" \
    -v "$STATE_DIR:/state" -v "$BAG:/bag:ro" \
    -e CYCLONEDDS_URI=file:///cfg/cyclonedds_host.xml -e ROS_DOMAIN_ID=25 \
    -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
    vipee/ros2-jazzy-android bash -c "
      source /opt/ros/jazzy/setup.bash
      source /ws/ros_test/install_sirv/setup.bash
      $1" >/dev/null
  sleep 3  # contêiner escutando a 7400 antes da partida da ponte
}
bridge() {  # <veículo>: mapa /vendor/etc/vipee/maps/<veículo>.yaml
  sh "setprop vendor.vipee.vehicle $1; stop vipee_bridge; start vipee_bridge"
}
state() {  # escrita atômica: o fake_sim relê pelo mtime
  echo "$1" > "$STATE_DIR/s.tmp" && mv "$STATE_DIR/s.tmp" "$STATE_DIR/state.json"
}
cams() {  # <modo front> <rear> <left> <right>
  echo "\"cameras\": {\"front\": \"$1\", \"rear\": \"$2\", \"left\": \"$3\", \"right\": \"$4\"}"
}
# Quadros do fake_sim: um JPEG por câmera tirado do bag, mais um JPEG 480x480
# e um PNG de verdade (Pillow do host).
frames() {
  [ -f "$STATE_DIR/front.jpg" ] && return
  docker run --rm --entrypoint '' --user "$(id -u):$(id -g)" \
    -v "$BAG:/bag:ro" -v "$HERE:/emu:ro" -v "$STATE_DIR:/out" \
    vipee/ros2-jazzy-android bash -c \
    "source /opt/ros/jazzy/setup.bash && python3 /emu/extract_frames.py /bag /out" >/dev/null
  python3 -c "from PIL import Image
im = Image.open('$STATE_DIR/front.jpg')
im.resize((480, 480)).save('$STATE_DIR/small.jpg', quality=80)
im.save('$STATE_DIR/frame.png')"
}
# fake_sim no namespace do Commander, ponte religada com o mapa <veículo>.
sim() {  # <veículo> <json do estado>
  frames
  state "$2"
  src_run "exec python3 /emu/fake_sim.py commander /state/state.json /state"
  bridge "$1"
}
bag_play() {
  src_run "exec ros2 bag play --loop /bag --topics \
    $(for p in $POS; do printf '/sirv/sensor/fisheye/%s/image/compressed ' "$p"; done) \
    /commander/telemetry /apply_control_to_vehicle"
  bridge commander
}
cam_log() { sh logcat -d -s vipee_bridge | grep -E " CAM $1( |\$)" || true; }
# Espera até <regex> aparecer no logcat da ponte (limite em s).
until_log() {  # <regex> <segundos>
  local end=$(($(date +%s) + $2))
  until sh logcat -d -s vipee_bridge | grep -qE "$1"; do
    [ "$(date +%s)" -gt "$end" ] && return 1
    sleep 0.5
  done
}
# Confere >= 8 quadros/s e 1280x720 de <id> na saída do probe de <s> segundos.
check_rate() {  # <out> <id> <segundos> <rótulo>
  local n w h
  n="$(field "$1" "$2" frames)"; w="$(field "$1" "$2" width)"; h="$(field "$1" "$2" height)"
  [ -n "$n" ] || fail "$4: $2 sem linha PROBE ($1)"
  [ "$n" -ge $((8 * $3)) ] || fail "$4: $2 com $n quadros em $3 s (< 8/s)"
  [ "$w" = "$W" ] && [ "$h" = "$H" ] || fail "$4: $2 em ${w}x${h}, esperado ${W}x${H}"
}
dev_of() {  # <posição> ➔ /dev/videoN do mapa
  case $1 in front) echo /dev/video10;; rear) echo /dev/video11;;
    left) echo /dev/video12;; right) echo /dev/video13;; esac
}
# A ponte LIVE nas 4 (depois de bag_play ou sim com as 4 em ok).
wait_live() {
  local p
  for p in $POS; do
    until_log " CAM $p LIVE" 60 || fail "$1: CAM $p não ficou LIVE: $(cam_log "$p" | tail -3)"
  done
}

step_bag() {
  echo "== bag"
  sh logcat -c
  bag_play
  wait_live bag
  sleep 2
  sh logcat -c
  local out p d st
  out="$(sh $PROBE "$BAG_S" $DEVS)" || fail "bag: probe falhou: $out"
  echo "$out"
  for d in $DEVS; do check_rate "$out" "$d" "$BAG_S" bag; done
  st="$(sh logcat -d -s vipee_bridge | grep ' STATS ' || true)"
  for p in $POS; do
    echo "$st" | grep -q " CAM $p STATS frames=[1-9]" || fail "bag: sem STATS com quadros de $p"
  done
  # p95_ms de todo STATS do período (ms, do recebimento no processo ROS até o
  # quadro escrito no dispositivo).
  echo "$st" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^p95_ms=/) {
      split($i, kv, "="); if (kv[2] + 0 > max) max = kv[2] + 0 } }
    END { printf "  STATS p95_ms máximo: %.1f ms\n", max; exit !(max <= 100) }' ||
    fail "bag: p95 acima de 100 ms: $st"
  avc_check
  echo "bag OK"
}

step_stale() {
  echo "== stale"
  docker inspect "$SRC" >/dev/null 2>&1 || { bag_play; wait_live stale; }
  sleep 2
  sh logcat -c
  docker pause "$SRC" >/dev/null
  sleep 0.6
  local out d p
  out="$(sh $PROBE 2 $DEVS)" || { docker unpause "$SRC"; fail "stale: probe falhou: $out"; }
  echo "$out"
  for d in $DEVS; do
    [ "$(field "$out" "$d" frames)" = 0 ] || { docker unpause "$SRC"; fail "stale: $d entregou quadro com o bag parado"; }
  done
  for p in $POS; do
    until_log " CAM $p STALE" 2 || { docker unpause "$SRC"; fail "stale: sem CAM $p STALE"; }
  done
  # Câmera aberta durante a pausa; o bag volta 2 s depois.
  (sleep 2; docker unpause "$SRC" >/dev/null) &
  out="$(sh $PROBE 6 $DEVS)" || fail "stale: probe da volta falhou: $out"
  wait
  echo "$out"
  for d in $DEVS; do
    [ "$(field "$out" "$d" frames)" -ge 24 ] || fail "stale: $d não retomou ($(field "$out" "$d" frames) quadros em ~4 s)"
    [ "$(field "$out" "$d" last_frame_ms_ago)" -lt 500 ] || fail "stale: $d parou de novo"
  done
  for p in $POS; do
    until_log " CAM $p LIVE" 2 || fail "stale: sem CAM $p LIVE na volta"
  done
  avc_check
  echo "stale OK"
}

step_drops() {
  echo "== drops"
  sh logcat -c
  sim commander "{\"speed\": 0.0, \"gear\": 2, $(cams ok ok ok ok)}"
  wait_live drops
  sh logcat -c  # só o que vem depois da troca para quadros ruins
  state "{\"speed\": 0.0, \"gear\": 2, $(cams small png trunc ok)}"
  sleep 1.5
  local out
  out="$(sh $PROBE 3 $DEVS)" || fail "drops: probe falhou: $out"
  echo "$out"
  for d in /dev/video10 /dev/video11 /dev/video12; do
    [ "$(field "$out" "$d" frames)" = 0 ] || fail "drops: $d entregou quadro inválido"
  done
  check_rate "$out" /dev/video13 3 "drops (right ok)"
  # Mais de 30 quadros ruins por câmera: um DROP só por motivo.
  local p want n
  for pw in front:FRAME_SIZE_MISMATCH rear:FORMAT_UNSUPPORTED left:DECODE_ERROR; do
    p=${pw%%:*} want=${pw#*:}
    n="$(cam_log "$p" | grep -c " DROP " || true)"
    [ "$n" = 1 ] || fail "drops: $p com $n linhas DROP, esperado 1: $(cam_log "$p")"
    cam_log "$p" | grep -q " CAM $p DROP $want\$" || fail "drops: $p sem DROP $want: $(cam_log "$p")"
    cam_log "$p" | grep -q " CAM $p STALE\$" || fail "drops: $p sem STALE"
  done
  ! cam_log right | grep -qE ' (DROP|STALE)' || fail "drops: right afetada: $(cam_log right)"
  # Motivo muda (480x480 ➔ PNG): novo DROP de front.
  state "{\"speed\": 0.0, \"gear\": 2, $(cams png png trunc ok)}"
  until_log " CAM front DROP FORMAT_UNSUPPORTED\$" 3 || fail "drops: front sem DROP na mudança de motivo"
  avc_check
  echo "drops OK"
}

step_device() {
  echo "== device"
  local map=/vendor/etc/vipee/maps/vipee_test_baddev.yaml pid out d
  "$ADB" remount >/dev/null 2>&1 || fail "adb remount"
  sed 's#/dev/video10#/dev/video99#' "$HERE/../../maps/commander.yaml" |
    "$ADB" shell "cat > $map"
  sh restorecon "$map"
  sh logcat -c
  # 36 km/h ➔ 10 m/s no VHAL.
  sim vipee_test_baddev "{\"speed\": 36.0, \"gear\": 2, $(cams ok ok ok ok)}"
  until_log " CAM front DEVICE_UNAVAILABLE" 30 || fail "device: sem CAM front DEVICE_UNAVAILABLE"
  pid="$(sh pidof vipee_bridge)"
  out="$(sh $PROBE 5 /dev/video11 /dev/video12 /dev/video13)" || fail "device: probe falhou: $out"
  echo "$out"
  for d in /dev/video11 /dev/video12 /dev/video13; do check_rate "$out" "$d" 5 device; done
  sh cmd car_service get-property-value 0x11600207 2>&1 |
    grep -qE 'Value: (10\.0|9\.99)[0-9]* METER_PER_SEC' ||
    fail "device: VHAL sem a velocidade: $(sh cmd car_service get-property-value 0x11600207 2>&1)"
  [ "$(sh pidof vipee_bridge)" = "$pid" ] || fail "device: a ponte reiniciou"
  [ "$(cam_log front | grep -c DEVICE_UNAVAILABLE)" = 1 ] ||
    fail "device: DEVICE_UNAVAILABLE repetido: $(cam_log front)"
  avc_check
  sh rm -f "$map"
  bridge commander
  echo "device OK"
}

step_install() {
  echo "== install"
  stop_src
  "$HERE/../jazzy/install-vendor.sh" evs-remove
  boot
  sh $PROBE --list | grep -qx 'CAMERA cam_front' || fail "evs-remove não devolveu os MP4"
  "$HERE/../jazzy/install-vendor.sh" evs
  setup
  local list d t i=10
  list="$(sh $PROBE --list)"
  for d in $DEVS; do
    echo "$list" | grep -qx "CAMERA $d" || fail "install: probe não lista $d: $list"
  done
  # Tipo do CarEvsManager ➔ dispositivo (CAM-19).
  for t in FRONTVIEW REARVIEW LEFTVIEW RIGHTVIEW; do
    sh cmd car_service get-camera-id $t | grep -q "/dev/video$i for $t" ||
      fail "install: $t não usa /dev/video$i"
    i=$((i + 1))
  done
  avc_check
  echo "install OK"
}

setup
steps=("$@")
[ $# -gt 0 ] || steps=(platform stall bag stale drops device install)
for s in "${steps[@]}"; do
  "step_$s"
done
echo "test-cam: OK (${steps[*]})"
