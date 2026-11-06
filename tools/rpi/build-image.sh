#!/bin/bash
# Gera a imagem de cartão SD da placa na VM e a traz para o WSL (RPI-01).
#
#   tools/rpi/build-image.sh <rpi4|rpi5>
#
# Na VM, no tmux "rpim" (log ~/rpim.log): lunch aosp_<placa>_car-bp4a-userdebug,
# m bootimage systemimage vendorimage, <placa>-mkimg.sh (README do
# raspberry-vanilla) e xz. Copia o .img.xz para $IMG_DIR. Rode o rpi-sync.sh
# antes. Um m por VM: avise os outros projetos (../vipee-shared/SHARED.md).
#
# Variáveis: RPI_HOST (vipee), RPI_TREE, IMG_DIR, JOBS (16: a VM tem 61 GB sem
# swap e o m com 32 jobs morre por OOM na fase de dex, r8/d8 com até 4 GB cada).
set -euo pipefail
board=${1:?uso: build-image.sh <rpi4|rpi5>}
case $board in rpi4|rpi5) ;; *) echo "placa desconhecida: $board" >&2; exit 2;; esac
RPI_HOST=${RPI_HOST:-vipee}
RPI_TREE=${RPI_TREE:-/home/wendell/aosp-rpi}
IMG_DIR=${IMG_DIR:-$HOME/rpi_images}
JOBS=${JOBS:-16}
out=$RPI_TREE/out/target/product/$board
job=/tmp/vipee-build-image.sh

# Roda na VM. env -i: o shell da VM não vaza variáveis para o build; USER é o
# dono da árvore, que o mkimg usa no chown da imagem.
ssh "$RPI_HOST" "cat > $job" <<EOF
#!/bin/bash
set -e
cd $RPI_TREE
rm -f $out/*-${board}_car.img $out/*-${board}_car.img.xz
env -i HOME=\$HOME USER=\$(whoami) PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  TERM=xterm bash -c 'source build/envsetup.sh && lunch aosp_${board}_car-bp4a-userdebug \
  && m -j$JOBS bootimage systemimage vendorimage && ./${board}-mkimg.sh'
xz -T0 -f $out/*-${board}_car.img
EOF
ssh "$RPI_HOST" "chmod +x $job; rm -f ~/rpim.log
tmux new-session -d -s rpim 'bash -c \"$job > ~/rpim.log 2>&1; echo exit=\\\$? >> ~/rpim.log\"'"
ssh -o ServerAliveInterval=30 "$RPI_HOST" 'until grep -q "^exit=" ~/rpim.log 2>/dev/null; do sleep 30; done
grep -E "FAILED|error:" ~/rpim.log | head -20; tail -3 ~/rpim.log'
rc=$(ssh "$RPI_HOST" 'sed -n "s/^exit=//p" ~/rpim.log')
[ "$rc" = 0 ] || exit "$rc"

img=$(ssh "$RPI_HOST" "ls $out/*-${board}_car.img.xz")
mkdir -p "$IMG_DIR"
scp -q "$RPI_HOST:$img" "$IMG_DIR/"
ls -l "$IMG_DIR/$(basename "$img")"
