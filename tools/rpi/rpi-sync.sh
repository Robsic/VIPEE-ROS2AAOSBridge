#!/usr/bin/env bash
# Monta vendor/vipee/bridge na árvore aosp-rpi (AD-020) e liga a camada aos
# produtos aosp_rpi{4,5}_car. Roda no WSL; escreve só na árvore de destino
# (VM vipee por padrão). Rodar de novo deixa a árvore igual.
#
# Pré-requisitos: ABI=arm64 tools/jazzy/run.sh /bridge/tools/jazzy/build-bridge.sh
# (push_arm64/lib e bridge_build_arm64/vipee_bridge) e um llvm-readelf.
#
# Variáveis:
#   RPI_HOST  host ssh da árvore (padrão vipee); vazio = árvore local
#   RPI_TREE  raiz da árvore (padrão /home/wendell/aosp-rpi)
#   WS        workspace ROS para Android (padrão ~/ros2_android_ws)
#   KO_DIR    v4l2loopback.ko por placa, opcional: $KO_DIR/rpi{4,5}/v4l2loopback.ko
#             (padrão $WS/rpi_modules; sem o arquivo a placa sai sem câmeras)
#   READELF   llvm-readelf (padrão: o clang mais novo dos prebuilts do ~/aosp)
#
# Layout de vendor/vipee/bridge:
#   Android.bp, vipee_rpi.mk, BoardConfigVipee.mk, init.vipee_evs_rpi.rc  (android/rpi)
#   maps/*.yaml, cyclonedds_rpi.xml
#   vhal/ evs_hal/ evs/ evs_probe/ sepolicy/
#   prebuilts/{Android.bp (gen_prebuilts.py), vipee_bridge, vipee_bridge.rc, lib/*.so}
#   modules/rpi{4,5}/v4l2loopback.ko
#
# Depois, na VM: lunch aosp_rpi4_car-bp4a-userdebug && m nothing
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RPI_HOST="${RPI_HOST-vipee}"
RPI_TREE="${RPI_TREE:-/home/wendell/aosp-rpi}"
WS="${WS:-$HOME/ros2_android_ws}"
KO_DIR="${KO_DIR:-$WS/rpi_modules}"
READELF="${READELF:-$(ls -d "$HOME"/aosp/prebuilts/clang/host/linux-x86/clang-r*/bin/llvm-readelf 2>/dev/null | sort -V | tail -1)}"
LIBS="$WS/push_arm64/lib"
BIN="$WS/bridge_build_arm64/vipee_bridge"

for f in "$LIBS/librclcpp.so" "$BIN" "$READELF"; do
  [ -f "$f" ] || { echo "ERRO: falta $f (build arm64 ou READELF)" >&2; exit 1; }
done

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
B="$STAGE/bridge"
mkdir -p "$B/vhal/include/vipee" "$B/evs_probe" "$B/maps" "$B/prebuilts/lib"

cp -p "$REPO"/android/rpi/* "$B/"
cp -p "$REPO"/android/vhal/{Android.bp,*.cpp,*.h,*.rc,*.json,*.xml} "$B/vhal/"
cp -p "$REPO"/android/vhal/include/vipee/*.h "$REPO/core/include/vipee/wire.h" "$B/vhal/include/vipee/"
cp -rp "$REPO/android/evs_hal" "$B/evs_hal"
cp -rp "$REPO/android/evs" "$B/evs"
cp -p "$REPO"/android/evs_probe/{Android.bp,*.cpp} "$B/evs_probe/"
cp -rp "$REPO/tools/jazzy/sepolicy" "$B/sepolicy"
cp -p "$REPO"/maps/*.yaml "$B/maps/"
cp -p "$REPO/tools/jazzy/config/cyclonedds_rpi.xml" "$B/"
cp -p "$REPO/tools/jazzy/vendor/vipee_bridge.rc" "$BIN" "$B/prebuilts/"
cp -p "$LIBS"/*.so "$B/prebuilts/lib/"
python3 "$REPO/tools/rpi/gen_prebuilts.py" --lib-dir "$B/prebuilts/lib" \
  --binary "$B/prebuilts/vipee_bridge" --init-rc vipee_bridge.rc \
  --readelf "$READELF" --out "$B/prebuilts/Android.bp"
for board in rpi4 rpi5; do
  if [ -f "$KO_DIR/$board/v4l2loopback.ko" ]; then
    mkdir -p "$B/modules/$board"
    cp -p "$KO_DIR/$board/v4l2loopback.ko" "$B/modules/$board/"
  else
    echo "aviso: sem $KO_DIR/$board/v4l2loopback.ko; $board sai sem câmeras (RPI-12)" >&2
  fi
done

# Roda o script da entrada padrão na máquina da árvore.
on_tree() { if [ -n "$RPI_HOST" ]; then ssh "$RPI_HOST" bash -s -- "$@"; else bash -s -- "$@"; fi; }
dest() { if [ -n "$RPI_HOST" ]; then echo "$RPI_HOST:$1"; else echo "$1"; fi; }

on_tree "$RPI_TREE" <<'EOF'
set -euo pipefail
mkdir -p "$1/vendor/vipee/bridge"
EOF
rsync -rlpt --delete --checksum "$B/" "$(dest "$RPI_TREE/vendor/vipee/bridge/")"

# Uma linha por arquivo, sempre a última (o filter-out do BoardConfigVipee.mk
# precisa rodar depois do permissive da placa): apaga a nossa e reescreve no fim.
on_tree "$RPI_TREE" <<'EOF'
set -euo pipefail
tag='# VIPEE ROS2AAOSBridge (tools/rpi/rpi-sync.sh)'
put_last() {  # arquivo linha
  [ -f "$1" ] || { echo "ERRO: falta $1" >&2; exit 1; }
  grep -vxF -e "$tag" -e "$2" "$1" > "$1.vipee" || true
  printf '%s\n%s\n' "$tag" "$2" >> "$1.vipee"
  cat "$1.vipee" > "$1" && rm "$1.vipee"
}
for n in 4 5; do
  d="$1/device/brcm/rpi$n"
  put_last "$d/aosp_rpi${n}_car.mk" '$(call inherit-product, vendor/vipee/bridge/vipee_rpi.mk)'
  put_last "$d/BoardConfig.mk" '-include vendor/vipee/bridge/BoardConfigVipee.mk'
  tail -n 2 "$d/aosp_rpi${n}_car.mk" "$d/BoardConfig.mk"
done
EOF
