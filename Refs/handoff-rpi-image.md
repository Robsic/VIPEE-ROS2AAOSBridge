# Handoff: imagem AAOS 16 com a ponte para Raspberry Pi 4 e 5

Roteiro para quem tem a placa na mão: gravar o cartão SD, ligar a placa ao host do simulador (WSL) e conferir telemetria e câmeras. Feature `rpi-image` (AD-019, AD-020, AD-021). Quem gerou a imagem não tinha a placa: nada abaixo rodou numa RPi ainda. Anote o que divergir na seção [Problemas conhecidos](#problemas-conhecidos).

Em cada passo de conferência, o bloco `#` diz a saída esperada. Comandos `adb` agem sobre a placa: rode só depois de decidir testar nela.

## 1. O que é a imagem

- Base: [raspberry-vanilla](https://github.com/raspberry-vanilla/android_local_manifest) `android-16.0` (AOSP 16, `android-16.0.0_r4`, o mesmo do emulador), produto `aosp_rpi4_car` ou `aosp_rpi5_car`, `userdebug`.
- Embutido no vendor: `vipee_bridge` e as libs ROS 2 Jazzy, VHAL da ponte (`vipee-vehicle-service-rpi`, no lugar do VHAL padrão), fork da HAL EVS (`vipee-evs-v4l2`, no lugar da `evs-v4l2` stock), `v4l2loopback.ko` com as câmeras em `/dev/video40..43`, mapas dos veículos e política SELinux.
- SELinux **enforcing** (a placa vem permissive de fábrica; a imagem tira o `androidboot.selinux=permissive`). Recuo na [seção 8](#8-recuo-para-permissive).
- A ponte não acha o host sozinha: o endereço vem da propriedade `persist.vendor.vipee.host` (seção 4). Sem ela, a ponte fica parada com `DDS_PEER_UNSET` no log.

## 2. Baixar e gravar o cartão

A imagem da RPi4 fica no WSL em `~/rpi_images/RaspberryVanillaAOSP16-20261106-rpi4_car.img.xz` (662 MB; gerada em 2026-11-06 por `tools/rpi/build-image.sh`, commit `a1c06af`). A da RPi5 ainda não foi gerada: espera as placas (T19); quando chegarem, `tools/rpi/build-image.sh rpi5`. Cartão de 16 GB ou mais; a imagem descomprimida tem 15,36 GB.

**Windows (recomendado):** Raspberry Pi Imager ➔ *Choose OS* ➔ *Use custom* ➔ o `.img.xz` (aceita xz direto) ➔ escolha o cartão ➔ *Write*. Copie o arquivo para o Windows antes (`cp ~/rpi_images/*.img.xz /mnt/c/Users/<você>/Downloads/`). Recuse as opções de "OS customisation": são do Raspberry Pi OS.

**Linux (com o cartão em `/dev/sdX`, confira com `lsblk` antes; o `dd` apaga o disco escolhido):**

```bash
xz -dc RaspberryVanillaAOSP16-20261106-rpi4_car.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

Antes de gravar, dá para checar a imagem sem placa:

```bash
tools/rpi/check-image.sh rpi4 ~/rpi_images/RaspberryVanillaAOSP16-20261106-rpi4_car.img.xz
# check-image rpi4: OK
```

## 3. Ligar a placa

1. Monitor HDMI na porta **HDMI0** (a mais perto da USB-C de energia), teclado e mouse USB.
2. Rede: cabo Ethernet na mesma LAN do PC do WSL (recomendado; o Wi-Fi pode filtrar e atrasa o teste). Wi-Fi funciona pelo Settings do AAOS.
3. ADB: a USB-C da placa é também a porta de dados do ADB (gadget `ffs.adb` em `device/brcm/rpi4/ramdisk/init.rpi4.usb.rc`); foi por ela que a placa foi acessada no handoff do Jazzy. A porta USB do PC pode não dar corrente para a placa: use um divisor USB-C com entrada de energia, ou passe o ADB para a rede (abaixo) e volte à fonte oficial.
4. Fonte: oficial, 5 V 3 A (RPi4) ou 5 V 5 A (RPi5). Fonte fraca trava o boot do AAOS sem aviso.

O primeiro boot leva alguns minutos (o AAOS formata o userdata). Fim esperado: a tela inicial do carro.

### ADB

No WSL, o `adb` precisa enxergar o USB do Windows (`usbipd attach --wsl --busid <id>`, do pacote usbipd-win) ou use o `adb.exe` do Windows. Com a placa ligada pelo USB-C e a depuração USB ativa (Settings ➔ System ➔ About ➔ toque 7× em *Build number*; depois Developer options ➔ *USB debugging*):

```bash
ADB=~/aosp/out/host/linux-x86/bin/adb
$ADB devices
# <serial>    device
$ADB root
$ADB tcpip 5555          # opcional: passa o adb para a rede; desligue o cabo depois
$ADB connect <IP_DA_PLACA>:5555
# connected to <IP_DA_PLACA>:5555
export ANDROID_SERIAL=<IP_DA_PLACA>:5555   # ou o <serial> USB; os comandos abaixo usam este device
```

O IP da placa está em Settings ➔ Network. O `tcpip` não sobrevive a reboot: depois de reiniciar, repita pelo cabo.

Se o emulador também estiver no ar, o `ANDROID_SERIAL` evita mandar comando para o device errado.

## 4. Conferir o boot

```bash
$ADB shell getenforce
# Enforcing
$ADB shell getprop ro.product.name
# aosp_rpi4_car
$ADB shell 'lsmod | grep v4l2loopback; ls /dev/video4*'
# v4l2loopback ...
# /dev/video40 /dev/video41 /dev/video42 /dev/video43
$ADB shell 'ps -A -Z | grep -E "vipee|evs-v4l2"'
# u:r:vendor_vipee_ros2:s0 ... vipee_bridge
# u:r:hal_vehicle_default:s0 ... vipee-vehicle-service-rpi
# u:r:hal_evs_default:s0 ... vipee-evs-v4l2
$ADB logcat -d -s vipee_bridge | grep DDS_PEER
# ... DDS_PEER_UNSET ...   (normal até definir o host)
$ADB shell 'dmesg | grep -i "avc:.*denied" | grep -i vipee'
# (nada)
```

## 5. Expor a porta 7400 do WSL na LAN

A ponte na placa disca o host por TCP na porta 7400 e não abre porta de escuta: basta o PC aceitar conexão de entrada na 7400 e repassá-la ao WSL. O WSL deste projeto está em modo NAT (o emulador depende disso); não troque para `networkingMode=mirrored`.

No **PowerShell como administrador** do Windows:

```powershell
$wsl = (wsl hostname -I).Trim().Split(" ")[0]
netsh interface portproxy add v4tov4 listenport=7400 listenaddress=0.0.0.0 connectport=7400 connectaddress=$wsl
New-NetFirewallRule -DisplayName "VIPEE DDS 7400" -Direction Inbound -Protocol TCP -LocalPort 7400 -Action Allow
netsh interface portproxy show v4tov4
# 0.0.0.0  7400  <IP do WSL>  7400
```

O IP do WSL muda a cada reinício do Windows: refaça o `portproxy add` (o `New-NetFirewallRule` fica). Para remover: `netsh interface portproxy delete v4tov4 listenport=7400 listenaddress=0.0.0.0`.

O endereço que a placa usa é o **IP do PC Windows na LAN** (`ipconfig`, adaptador Ethernet ou Wi-Fi), não o do WSL.

## 6. Telemetria: `fake_sim` ➔ CarService

No WSL, na raiz do repositório, suba o simulador falso (o mesmo do `tools/emu/test-e2e.sh`):

```bash
mkdir -p /tmp/vipee_state
# speed em km/h (18 = 5 m/s), lever em ASCII (68 = 'D'), handbrake booleano
echo '{"speed": 18.0, "gear": 2, "lever": 68, "handbrake": false}' > /tmp/vipee_state/state.json
docker run -d --rm --name vipee_rpi_sim --network host \
  -v $PWD/tools/jazzy/config:/cfg:ro -v ~/ros2_android_ws:/ws:ro -v $PWD/tools/emu:/emu:ro \
  -v /tmp/vipee_state:/state:ro \
  -e CYCLONEDDS_URI=file:///cfg/cyclonedds_host.xml -e ROS_DOMAIN_ID=25 \
  -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp vipee/ros2-jazzy-android bash -c '
    source /opt/ros/jazzy/setup.bash && source /ws/ros_test/install_sirv/setup.bash &&
    exec python3 /emu/fake_sim.py cat793f /state/state.json'
```

Sem o terceiro argumento o `fake_sim` não publica câmeras. O formato do `state.json` está no cabeçalho de `tools/emu/fake_sim.py`; ele relê o arquivo quando muda, então dá para trocar velocidade e alavanca com a placa ligada. Defina o host na placa e reinicie a ponte, que só disca na partida:

```bash
$ADB shell setprop persist.vendor.vipee.host <IP_DO_PC_NA_LAN>
$ADB shell 'stop vipee_bridge; start vipee_bridge'
$ADB logcat -d -s vipee_bridge | grep -E "DDS_PEER|ERR" | tail -3
# (sem DDS_PEER_UNSET nem DDS_PEER_INVALID depois do restart)
$ADB shell cmd car_service get-property-value 0x11600207   # PERF_VEHICLE_SPEED
# ... SystemStatus: AVAILABLE ... floatValues: [5.0] ...
$ADB shell cmd car_service get-property-value 0x11400400   # GEAR_SELECTION
# ... SystemStatus: AVAILABLE ... int32Values: [8] ...   (DRIVE)
$ADB shell cmd car_service get-property-value 0x11200402   # PARKING_BRAKE_ON
# ... SystemStatus: AVAILABLE ... int32Values: [0] ...
```

A propriedade é `persist`: sobrevive a reboot. Dado velho: pare o simulador (`docker stop vipee_rpi_sim`) e repita o `get-property-value`; em menos de 1 s as três ficam `SystemStatus: UNAVAILABLE` e o logcat da ponte mostra `STALE`.

Latência (RPI-09): `tools/emu/test-e2e.sh` mede publicação ➔ `CarService` via adb; com `ADB="$ADB -s <IP_DA_PLACA>:5555"` e o `fake_sim` acima parado, ele roda contra a placa. O número inclui o adb por rede (limite superior). Anote no `validation.md` da feature.

## 7. Câmeras: bag ➔ EVS

Pare o `fake_sim` (um participante DDS só no host) e toque só as câmeras do bag de referência de 2026-11-05 (o do fluxo de ponta a ponta; extraído de `data/rosbag2_calibration_2026_11_05.zip` do CalibDroid). Ele tem ~3,7 quadros/s por câmera: para conferir a taxa (≥ 8 quadros/s, RPI-11) toque com `--rate 3`; para calibrar, 1x.

```bash
docker stop vipee_rpi_sim
docker run -d --rm --name vipee_rpi_bag --network host --entrypoint '' \
  -v $PWD/tools/jazzy/config:/cfg:ro -v ~/ros2_android_ws:/ws:ro \
  -v ~/rosbags/rosbag2_2026_11_05-15_05_39/rosbag2_2026_11_05-15_05_39:/bag:ro \
  -e CYCLONEDDS_URI=file:///cfg/cyclonedds_host.xml -e ROS_DOMAIN_ID=25 \
  -e RMW_IMPLEMENTATION=rmw_cyclonedds_cpp vipee/ros2-jazzy-android bash -c \
  'source /opt/ros/jazzy/setup.bash && source /ws/ros_test/install_sirv/setup.bash &&
   ros2 bag play --loop --rate 3 /bag --topics /sirv/sensor/fisheye/{front,rear,left,right}/image/compressed'
$ADB shell setprop vendor.vipee.vehicle commander
$ADB shell 'stop vipee_bridge; start vipee_bridge'
$ADB logcat -d -s vipee_bridge | grep STATS | tail -4
# CAM front STATS frames=<n> p95_ms=<x> drops=0   (uma linha por câmera, n > 0)
$ADB shell /vendor/bin/vipee_evs_probe 5 /dev/video40 /dev/video41 /dev/video42 /dev/video43
# uma linha por câmera: 1280x720, >= 8 quadros/s
```

Para os apps (CalibDroid, AVM) a câmera chega pelo `CarEvsManager`: `adb shell cmd car_service get-camera-id FRONTVIEW` deve responder `/dev/video40`. Para calibrar, injete o veículo parado como no README (`inject-vhal-event` de P, freio e velocidade 0).

## 8. Recuo para permissive

Só se o enforcing travar o teste e não houver tempo de corrigir a política. Grave o `avc: denied` antes (`$ADB shell dmesg | grep avc` e `$ADB logcat -d | grep avc`), para virar correção.

1. Desligue a placa e ponha o cartão no PC. A partição 1 (FAT, rótulo `boot`) abre no Windows.
2. Edite `cmdline.txt` (uma linha só) e acrescente ` androidboot.selinux=permissive` no fim.
3. Volte o cartão e ligue. `adb shell getenforce` deve responder `Permissive`.

Para voltar, apague o trecho acrescentado.

## Problemas conhecidos

Itens deduzidos do build, não vistos na placa. Confirme ou corrija ao testar.

| Sintoma | Causa provável | Correção |
| --- | --- | --- |
| `DDS_PEER_UNSET` no logcat da ponte | `persist.vendor.vipee.host` vazio | Seção 6: `setprop` e restart da ponte |
| `DDS_PEER_INVALID` | Endereço com porta, espaço ou nome que não resolve | Use o IP puro do PC, sem `:7400` |
| Propriedades `UNAVAILABLE` com o `fake_sim` no ar | Porta 7400 fechada no Windows ou IP do WSL mudou | Seção 5: refaça o `portproxy add`; de outro PC, `Test-NetConnection <IP_DO_PC> -Port 7400` |
| Câmeras e telemetria param (`STATS frames=0`) e o player do host loga `tcp abandoning write` | Escrita do host bloqueada > 2 s (placa ou rede lenta): o Cyclone do host larga a conexão e não consegue discar a placa de volta (visto no emulador em 2026-11-05; correção pendente, ver STATE) | `adb shell 'stop vipee_bridge; start vipee_bridge'`; prefira Ethernet |
| Sem `/dev/video40..43`; ponte loga `DEVICE_UNAVAILABLE` por câmera | `v4l2loopback.ko` não carregou | `adb shell dmesg | grep -i v4l2loopback`. `disagrees about version of symbol` ou `invalid module format` = módulo fora de sincronia com o kernel: refaça `tools/rpi/build-kernel-module.sh` e a imagem. A telemetria segue funcionando |
| Câmeras sem quadros, `STATS frames=0` | Bag não está tocando, veículo não é `commander` ou ponte não reiniciou | Seção 7 na ordem |
| App de câmera vê imagem errada ou nenhuma | RRO de câmeras ausente ou codec ocupando o número | `cmd car_service get-camera-id FRONTVIEW` deve dar `/dev/video40`; os codecs da RPi4 usam `/dev/video10..31` (AD-021) |
| App não recebe quadros de uma câmera; `evsmanagerd` loga `Skipping new frame as we hold 10 of 10` | Cliente EVS que não devolve buffers (vazamento do `CarEvsService`, `Refs/aosp-bug-carevsservice-stream-client-leak.md`) | `adb shell setprop ctl.restart android.hardware.automotive.evs-v4l2` (reinicia também `cardisplayproxyd` e `evsmanagerd`); avise quem usa o EVS |
| `avc: denied` com `vipee` no `dmesg` | Política SELinux incompleta na placa | Registre o `avc` (vira correção na política); recuo na seção 8 |
| Boot preso no logo ou reiniciando | Fonte fraca ou cartão lento | Fonte oficial; cartão A1/A2 |
| Tela preta | HDMI na porta errada | Use a HDMI0 |
| Popup "page size" ao abrir apps | Lib com alinhamento de 4 KB | As libs da ponte já saem com 16 KB (`max-page-size=16384`); se aparecer, anote qual app |

## Gerar a imagem de novo

Na ordem, do WSL (a VM `vipee` é compartilhada: avise os outros projetos antes de cada build, ver `../vipee-shared/SHARED.md`):

```bash
tools/jazzy/run.sh ...              # ponte arm64 (T2/T3; ver README)
tools/rpi/build-kernel-module.sh    # v4l2loopback.ko das duas placas
tools/rpi/rpi-sync.sh               # camada vendor na árvore ~/aosp-rpi da VM
tools/rpi/build-image.sh rpi4       # imagem em ~/rpi_images
tools/rpi/check-image.sh rpi4 ~/rpi_images/RaspberryVanillaAOSP16-<data>-rpi4_car.img.xz
```
