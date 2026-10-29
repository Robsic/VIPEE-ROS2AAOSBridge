#!/usr/bin/env bash
# Abre um shell (ou roda um comando) no contêiner Jazzy com o workspace do
# ROS para Android montado em /ws e o repositório em /bridge.
#   tools/jazzy/run.sh                 # shell interativo
#   tools/jazzy/run.sh colcon build …  # comando direto
# WS: diretório do workspace no host (padrão ~/ros2_android_ws), fora do repo.
set -euo pipefail

IMAGE=vipee/ros2-jazzy-android
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WS="${WS:-$HOME/ros2_android_ws}"
mkdir -p "$WS"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build -t "$IMAGE" "$REPO/tools/jazzy"
fi

# --network host: o emulador disca para a porta TCP do DDS no host (handoff §7).
TTY=(-i); [ -t 0 ] && TTY=(-it)
exec docker run --rm "${TTY[@]}" --network host \
  --user "$(id -u):$(id -g)" -e HOME=/ws \
  -v "$WS:/ws" -v "$REPO:/bridge" \
  "$IMAGE" "${@:-bash}"
