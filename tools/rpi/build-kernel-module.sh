#!/bin/bash
# Compila o v4l2loopback.ko do RPi4 e do RPi5 (RPI-10, RPI-14) na árvore de
# kernel da VM e copia cada um para $KO_DIR/<placa>/, onde o rpi-sync.sh o pega.
#
#   tools/rpi/build-kernel-module.sh [rpi4] [rpi5]     (padrão: as duas)
#
# O módulo é um ddk_module contra o kernel_build //common:<placa>, mesma fonte
# (common-android16-6.12 + manifesto raspberry-vanilla) do kernel pré-compilado
# em device/brcm/<placa>-kernel. Falha com VERMAGIC_MISMATCH se o vermagic do
# .ko não for igual ao de um módulo pré-compilado da placa; nesse caso o kernel
# inteiro tem de ser recompilado e trocado (README do raspberry-vanilla).
#
# Variáveis: RPI_HOST (vipee; vazio = local), KERNEL_TREE, RPI_TREE, KO_DIR,
# REPO_DIR. O .ko sai sem assinatura: o kernel tem MODULE_SIG sem MODULE_SIG_FORCE.
# Um bazel/m por VM: avise os outros projetos antes (../vipee-shared/SHARED.md).
set -euo pipefail
RPI_HOST=${RPI_HOST-vipee}
KERNEL_TREE=${KERNEL_TREE:-/home/wendell/aosp-rpi-kernel}
RPI_TREE=${RPI_TREE:-/home/wendell/aosp-rpi}
KO_DIR=${KO_DIR:-$HOME/ros2_android_ws/rpi_modules}
# --config=stamp lê o hash do git por `repo manifest`; sem repo no PATH o vermagic
# perde o sufixo -g<hash> e não casa com o kernel pré-compilado.
REPO_DIR=${REPO_DIR:-/home/wendell/aosp/.repo/repo}
boards=("$@"); [ ${#boards[@]} -gt 0 ] || boards=(rpi4 rpi5)

remote() { if [ -n "$RPI_HOST" ]; then ssh "$RPI_HOST" "$@"; else bash -c "$*"; fi; }
fetch() { if [ -n "$RPI_HOST" ]; then scp -q "$RPI_HOST:$1" "$2"; else cp "$1" "$2"; fi; }

# Pacote //vipee na árvore de kernel: fontes copiadas de common-modules (o
# filegroup de lá é privado) e um ddk_module por placa. O kernel_build da placa
# não declara ddk_module_headers, então os headers do kernel entram por deps.
build=""
targets=""
for b in "${boards[@]}"; do
  build+="
ddk_module(
    name = \"${b}_v4l2loopback\",
    srcs = glob([\"v4l2loopback/*.c\", \"v4l2loopback/*.h\"]),
    out = \"v4l2loopback.ko\",
    kernel_build = \"//common:${b}\",
    deps = [\"//common:all_headers_aarch64\"],
)
"
  targets+=" //vipee:${b}_v4l2loopback"
done
remote "set -e; cd $KERNEL_TREE; mkdir -p vipee/v4l2loopback
cp common-modules/virtual-device/v4l2loopback/*.[ch] vipee/v4l2loopback/
cat > vipee/BUILD.bazel <<'EOF'
# Gerado por VIPEE-ROS2AAOSBridge tools/rpi/build-kernel-module.sh; não editar.
load(\"//build/kernel/kleaf:kernel.bzl\", \"ddk_module\")
$build
EOF
PATH=$REPO_DIR:\$PATH tools/bazel build --config=fast --config=stamp $targets"

rc=0
for b in "${boards[@]}"; do
  ko=$(remote "find -L $KERNEL_TREE/bazel-bin/vipee/${b}_v4l2loopback -name v4l2loopback.ko | head -1")
  [ -n "$ko" ] || { echo "KO_NOT_FOUND $b" >&2; rc=1; continue; }
  ref=$(remote "find $RPI_TREE/device/brcm/$b-kernel -name '*.ko' | head -1")
  got=$(remote "modinfo -F vermagic $ko")
  want=$(remote "modinfo -F vermagic $ref")
  if [ "$got" != "$want" ]; then
    echo "VERMAGIC_MISMATCH $b: módulo '$got', kernel '$want'" >&2
    rc=1
    continue
  fi
  mkdir -p "$KO_DIR/$b"
  fetch "$ko" "$KO_DIR/$b/v4l2loopback.ko"
  echo "$b: $KO_DIR/$b/v4l2loopback.ko (vermagic $got)"
done
exit $rc
