#!/usr/bin/env bash
# Cross-compila o nó de teste (bringup/) contra o install Android x86_64 (handoff §4.2 "Nós de teste").
#   tools/jazzy/run.sh /bridge/tools/jazzy/build-bringup.sh
set -euo pipefail
I=/ws/ros2_src/install_android_x86_64
P=""; for d in $I/*/; do P="$P:${d%/}"; done
export AMENT_PREFIX_PATH="${P#:}" CMAKE_PREFIX_PATH="${P#:}"
cmake -S /bridge/tools/jazzy/bringup -B /ws/bringup_build_x86_64 \
  --toolchain /bridge/tools/jazzy/android_x86_64.toolchain.cmake \
  -DRMW_IMPLEMENTATION=rmw_cyclonedds_cpp -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS=-Wl,-z,max-page-size=16384 2>&1 | grep -E 'RMW|typesupport|Error|error' || true
cmake --build /ws/bringup_build_x86_64 -j"$(nproc)" 2>&1 | tail -3
