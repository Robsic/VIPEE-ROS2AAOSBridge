#!/usr/bin/env bash
# Gate de integração das câmeras no emulador (cam-evs-slice). Pré-requisito:
# tools/jazzy/install-vendor.sh evs (v4l2loopback + evs-v4l2) e o probe
# compilado (vm_m.sh vipee_evs_probe). Etapas:
#   platform - escritor do spike em 1280x720 YUYV nos /dev/video10..13: o probe
#              lista os 4 e recebe quadros 1280x720 a >= 8 Hz de cada; sem avc
#              de vendor_modprobe, hal_evs_default ou vendor_vipee_ros2
#              (CAM-19, CAM-21). Com REMOVE=1 também confere que evs-remove
#              devolve os MP4 e reinstala (CAM-20; dois reboots).
#   stall    - HAL com escritor parado (AD-016): câmera aberta sem escritor
#              abre, não recebe quadro e fecha em < 1 s; escritor que chega
#              depois entrega 1280x720; escritor que para com o fd aberto não
#              repete quadro e a câmera fecha em < 1 s; escritor que volta
#              retoma sem reabrir (CAM-04, CAM-06, CAM-07)
#   tools/emu/test-cam.sh [platform|stall]...   # padrão: platform stall
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

fail() { echo "FAIL: $*" >&2; exit 1; }
sh() { "$ADB" shell "$@" | tr -d '\r'; }

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

step_platform() {
  echo "== platform"
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
  if [ "${REMOVE:-0}" = 1 ]; then
    "$HERE/../jazzy/install-vendor.sh" evs-remove
    boot
    sh $PROBE --list | grep -qx 'CAMERA cam_front' || fail "evs-remove não devolveu os MP4"
    "$HERE/../jazzy/install-vendor.sh" evs
    setup
  fi
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
  echo "stall OK"
}

setup
for s in "${@:-platform stall}"; do
  "step_$s"
done
