#!/bin/bash
# Checagem estática da imagem da placa, sem montar nada (RPI-01..04).
#
#   tools/rpi/check-image.sh <rpi4|rpi5> <imagem.img[.xz]>
#   tools/rpi/check-image.sh <rpi4|rpi5> --parts <dir>   (dir com boot.img e vendor.img)
#
# Extrai a partição 1 (boot, FAT) e a 6 (vendor, ext4) com sfdisk/dd (sem
# mount, sem mtools; só debugfs e llvm-readelf) e confere:
# arquivos da ponte no vendor com o rótulo SELinux certo, v4l2loopback.ko,
# VHAL e EVS stock ausentes, NEEDED das libs sem .so.N fora do vendor e
# cmdline.txt sem androidboot.selinux=permissive. Cada falha imprime uma linha
# FAIL <motivo>; sai 1 se houver alguma.
set -euo pipefail
board=${1:?uso: check-image.sh <rpi4|rpi5> <imagem>|--parts <dir>}
READELF=${READELF:-$(command -v llvm-readelf || ls ~/aosp/prebuilts/clang/host/linux-x86/clang-r*/bin/llvm-readelf | tail -1)}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ "${2:-}" = --parts ]; then
  parts=${3:?falta o diretório}
else
  img=${2:?falta a imagem}
  if [[ $img == *.xz ]]; then xz -dc "$img" > "$work/disk.img"; img=$work/disk.img; fi
  extract() {  # extract <n> <saída>: copia a partição n da imagem
    read -r start size < <(sfdisk -d "$img" \
      | sed -n "s|^$img$1 : start= *\([0-9]*\), size= *\([0-9]*\).*|\1 \2|p")
    dd if="$img" of="$2" bs=512 skip="$start" count="$size" status=none
  }
  parts=$work
  extract 1 "$parts/boot.img"
  extract 6 "$parts/vendor.img"
fi

fails=0
fail() { echo "FAIL $*"; fails=$((fails + 1)); }
vfs() { debugfs -R "$1" "$parts/vendor.img" 2>/dev/null; }
label() {  # debugfs: security.selinux (37) = "u:object_r:x:s0\000"
  vfs "ea_get $1 security.selinux" | sed -n 's/^security.selinux ([0-9]*) = "\([^"\\]*\).*/\1/p'
}

# Arquivo no vendor e, se pedido, o rótulo.
declare -A want=(
  [/bin/vipee_bridge]=u:object_r:vendor_vipee_ros2_exec:s0
  [/bin/hw/vipee-vehicle-service-rpi]=u:object_r:hal_vehicle_default_exec:s0
  [/bin/hw/vipee-evs-v4l2]=u:object_r:hal_evs_default_exec:s0
  [/etc/init/vipee_bridge.rc]=
  [/etc/init/vipee-vehicle-service-rpi.rc]=
  [/etc/init/init.vipee_evs_rpi.rc]=
  [/etc/vipee/cyclonedds_device.xml]=
  [/etc/automotive/evs/evs_configuration_override.xml]=
  [/lib/modules/v4l2loopback.ko]=
  [/lib64/libc++_shared.so]=
)
for f in "${!want[@]}"; do
  if ! vfs "stat $f" | grep -q '^Inode:'; then fail "MISSING /vendor$f"; continue; fi
  [ -z "${want[$f]}" ] || [ "$(label "$f")" = "${want[$f]}" ] \
    || fail "LABEL /vendor$f: '$(label "$f")' (esperado ${want[$f]})"
done

for f in /bin/hw/android.hardware.automotive.vehicle@V4-default-service \
         /bin/hw/android.hardware.automotive.evs-v4l2; do
  vfs "stat $f" | grep -q '^Inode:' && fail "STOCK_PRESENT /vendor$f"
done

# NEEDED: tudo o que o binário e as libs do vendor pedem tem de estar no vendor
# ou ser lib do sistema sem versão (.so.N só vem do host).
mkdir -p "$work/elf"
vfs "rdump /lib64 $work/elf" >/dev/null
vfs "dump /bin/vipee_bridge $work/elf/vipee_bridge" >/dev/null
if [ -f "$work/elf/vipee_bridge" ]; then
  for e in "$work/elf/vipee_bridge" "$work"/elf/lib64/*.so; do
    "$READELF" -d "$e" | grep -oE 'Shared library: \[[^]]+\]' | { grep -E '\.so\.[0-9]' || true; } \
      | sed "s|^|NEEDED_VERSIONED $(basename "$e") |" | while read -r l; do echo "FAIL $l"; done
  done | tee "$work/needed"
  fails=$((fails + $(wc -l < "$work/needed")))
fi

# cmdline.txt (a linha com androidboot.hardware=) lido dos bytes do FAT: sem mtools
# nem mount. O mkbootimg.mk da placa grava BOARD_KERNEL_CMDLINE nele.
grep -a -m1 -o '[[:print:]]*androidboot[.]hardware=[[:print:]]*' "$parts/boot.img" > "$work/cmdline.txt" \
  || fail "MISSING boot/cmdline.txt"
grep -q 'androidboot.selinux=permissive' "$work/cmdline.txt" 2>/dev/null \
  && fail "PERMISSIVE boot/cmdline.txt: $(cat "$work/cmdline.txt")"
grep -q "androidboot.hardware=$board" "$work/cmdline.txt" 2>/dev/null \
  || fail "BOARD boot/cmdline.txt não é de $board"

if [ "$fails" -gt 0 ]; then echo "check-image $board: $fails falha(s)"; exit 1; fi
echo "check-image $board: OK"
