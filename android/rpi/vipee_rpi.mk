# VIPEE ROS2AAOSBridge na imagem do Raspberry Pi (rpi-image, AD-020).
# Herdado por device/brcm/rpi{4,5}/aosp_rpi{4,5}_car.mk (linha posta pelo
# tools/rpi/rpi-sync.sh). Uma peça por item que o install-vendor.sh empurra no
# emulador:
#   vipee_bridge                 binário, .rc (init_rc) e as libs ROS (required),
#                                todos em prebuilts/Android.bp, gerado por
#                                tools/rpi/gen_prebuilts.py
#   vipee_maps                   /vendor/etc/vipee/maps/*.yaml
#   vipee_cyclonedds_rpi.xml     /vendor/etc/vipee/cyclonedds_device.xml (disca
#                                ${VIPEE_HOST}, de persist.vendor.vipee.host)
#   vipee-vehicle-service-rpi    VHAL da ponte, .rc com o socket vipee_vhal e
#                                VINTF; overrides do V4-default-service
#   vipee_properties.json        configuração do VHAL (também required dele)
#   vipee-evs-v4l2               fork da HAL EVS (AD-016) e VINTF; overrides da
#                                evs-v4l2 stock
#   init.vipee_evs_rpi.rc        carrega o v4l2loopback e declara a HAL EVS
#   vipee_evs_configuration.xml  evs_configuration_override.xml das 4 câmeras
#   VipeeEvsCameraOverlay        RRO: tipos do CarEvsService ➔ /dev/video40..43
#   vipee_evs_probe              cliente EVS de teste (handoff)
# Política SELinux e v4l2loopback.ko: BoardConfigVipee.mk. ueventd: o da placa
# já cobre /dev/video* (ver init.vipee_evs_rpi.rc).

PRODUCT_PACKAGES += \
    vipee_bridge \
    vipee_maps \
    vipee_cyclonedds_rpi.xml \
    vipee-vehicle-service-rpi \
    vipee_properties.json \
    vipee-evs-v4l2 \
    init.vipee_evs_rpi.rc \
    vipee_evs_configuration.xml \
    VipeeEvsCameraOverlay \
    vipee_evs_probe
