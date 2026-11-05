#!/usr/bin/env bash
# Cross-compila o vipee_bridge (ros/vipee_bridge) contra o install Android
# (ABI abaixo) e atualiza as libs de push (handoff §4.2 "Nós de teste"). Mesmo
# esquema do build-bringup.sh: c++_shared, igual às libs ROS instaladas
# (um libc++ só), páginas de 16 KB. Rodar no contêiner, depois do
# build-android.sh:
#   tools/jazzy/run.sh /bridge/tools/jazzy/build-bridge.sh
set -euo pipefail
# ABI=x86_64 (emulador, padrão) | arm64 (RPi4/RPi5); mesma convenção do build-android.sh.
ABI="${ABI:-x86_64}"
case "$ABI" in
  x86_64) TRIPLE=x86_64-linux-android ;;
  arm64) TRIPLE=aarch64-linux-android ;;
  *) echo "ERRO: ABI=$ABI desconhecida (x86_64|arm64)" >&2; exit 1 ;;
esac
I=/ws/ros2_src/install_android_${ABI}
P=""; for d in $I/*/; do P="$P:${d%/}"; done
# Android primeiro; depois as ferramentas ament/rosidl nativas (handoff §4.2).
H=/ws/ros2_src/install
for d in $H/*/; do P="$P:${d%/}"; done
export AMENT_PREFIX_PATH="${P#:}" CMAKE_PREFIX_PATH="${P#:}"
PYTHONPATH="$(ls -d $H/*/lib/python3*/site-packages | paste -sd:)"
export PYTHONPATH
cmake -S /bridge/ros/vipee_bridge -B /ws/bridge_build_${ABI} \
  --toolchain /bridge/tools/jazzy/android_${ABI}.toolchain.cmake \
  -DRMW_IMPLEMENTATION=rmw_cyclonedds_cpp -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS=-Wl,-z,max-page-size=16384 2>&1 | grep -E 'RMW|typesupport|Error|error' || true
cmake --build /ws/bridge_build_${ABI} -j"$(nproc)" 2>&1 | tail -3

# Libs para o device: todas as .so do install Android (inclui sirv_msgs) e o
# libc++_shared do NDK. install-vendor.sh empurra este diretório.
L=/ws/push_${ABI}/lib
mkdir -p "$L"
find "$I" -name '*.so' -exec cp -u {} "$L/" \;
cp -u "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/${TRIPLE}/libc++_shared.so "$L/"

# Nenhuma lib versionada (.so.N) de fora do install Android, e LOAD a 16 KB.
BIN=/ws/bridge_build_${ABI}/vipee_bridge
if llvm-readelf -d "$BIN" | grep NEEDED | grep -E '\.so\.[0-9]'; then
  echo "ERRO: $BIN pede lib versionada" >&2; exit 1
fi
llvm-readelf -d "$BIN" | grep NEEDED
llvm-readelf -lW "$BIN" | awk '/LOAD/{print "LOAD align", $NF}' | sort -u
