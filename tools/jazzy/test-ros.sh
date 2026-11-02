#!/usr/bin/env bash
# Build nativo (host do contêiner, Jazzy) e teste de integração do vipee_bridge
# com mensagens sirv_msgs reais. Rodar no contêiner:
#   tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh
# Usa /opt/ros/jazzy de propósito, só aqui; build e install ficam em
# /ws/ros_test, separados do build cruzado Android (build-android.sh nunca
# vê o /opt/ros/jazzy).
set -euo pipefail

T=/ws/ros_test
SIRV_BASE="${SIRV_MSGS_BASE:-/sirv_src}"
# Os setup.bash do ament não toleram `set -u`.
set +u
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
mkdir -p "$T"
cd "$T"
colcon build --base-paths "$SIRV_BASE" --build-base build_sirv \
  --install-base install_sirv --packages-select sirv_msgs \
  --cmake-args -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
# shellcheck disable=SC1091
source install_sirv/setup.bash
set -u

# libjpeg-turbo nativa e estática (mesmo fonte fixado do build Android).
JT=/ws/third_party/libjpeg-turbo
cmake -S "$JT" -B build_jpeg -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$T/install_jpeg" -DENABLE_SHARED=OFF \
  -DENABLE_STATIC=ON -DWITH_JAVA=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
cmake --build build_jpeg -j"$(nproc)" >/dev/null
cmake --install build_jpeg >/dev/null
export CMAKE_PREFIX_PATH="$T/install_jpeg:${CMAKE_PREFIX_PATH:-}"

cmake -S /bridge/ros/vipee_bridge -B build_bridge -DVIPEE_ROS_TESTS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build_bridge -j"$(nproc)"

# Domínio e descoberta isolados do simulador; sem o XML TCP do device.
unset CYCLONEDDS_URI
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_DOMAIN_ID=77
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
ctest --test-dir build_bridge --output-on-failure
