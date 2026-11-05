# VIPEE ROS2AAOSBridge: parte de board da imagem do Raspberry Pi (AD-020).
# Incluído no FIM de device/brcm/rpi{4,5}/BoardConfig.mk pelo
# tools/rpi/rpi-sync.sh, para o filter-out rodar depois do
# `BOARD_KERNEL_CMDLINE += androidboot.selinux=permissive` da placa.

# Domínio vendor_vipee_ros2, propriedade do host e rótulos (tools/jazzy/sepolicy).
BOARD_VENDOR_SEPOLICY_DIRS += vendor/vipee/bridge/sepolicy

# Imagem em enforcing. Recuo sem rebuild: androidboot.selinux=permissive no
# cmdline.txt do SD (handoff).
BOARD_KERNEL_CMDLINE := $(filter-out androidboot.selinux=permissive,$(BOARD_KERNEL_CMDLINE))

# v4l2loopback da placa (T16), em /vendor/lib/modules com modules.dep gerado
# pelo build (o modprobe do init.vipee_evs_rpi.rc precisa dele). Opcional: sem
# o arquivo a imagem sai sem câmeras e o resto funciona (RPI-12).
BOARD_VENDOR_KERNEL_MODULES += $(wildcard vendor/vipee/bridge/modules/$(TARGET_DEVICE)/v4l2loopback.ko)
