# cam-evs-slice Tasks

## Execution Protocol (MANDATORY -- do not skip)

Implement these tasks with the `tlc-spec-driven` skill: **activate it by name and follow its Execute flow and Critical Rules.** Do not search for skill files by filesystem path. The skill is the source of truth for the full flow (per-task cycle, sub-agent delegation, adequacy review, Verifier, discrimination sensor).

**If the skill cannot be activated, STOP and tell the user - do not proceed without it.**

---

**Design**: `.specs/features/cam-evs-slice/design.md`
**Status**: Approved

---

## Test Coverage Matrix

> Generated from codebase, project guidelines, and spec - confirm before Execute. Guidelines found: `AGENTS.md` §3 (RED-GREEN-REFACTOR, núcleo sem ROS nem Android com fakes da interface própria, nunca mockar `rclcpp` nem HAL, integração marcada e separada, fail-loud), §3.1 (clang-format/clang-tidy por arquivo inteiro); amostras: `core/test/*_test.cpp`, `ros/vipee_bridge/test/bridge_node_rostest.cpp`, `tools/emu/test-*.sh` (ct-speed-slice).

| Code Layer | Required Test Type | Coverage Expectation | Location Pattern | Run Command |
| ---------- | ------------------ | -------------------- | ---------------- | ----------- |
| Núcleo (`core/`: câmeras no mapa, JPEG ➔ YUYV, `LatestSlot`, `CameraChannel`) | unit | Todos os ramos; 1:1 com CAM-01, 03, 04, 06, 07, 09..14, 16..18, 22..24; JPEGs reais gerados no teste com a própria libjpeg-turbo | `core/test/*_test.cpp` | `ctest --preset default -L unit` |
| `V4l2Sink` (Linux, sem ROS) | unit | Caminhos de falha (`open`, `S_FMT`, `write`) contra arquivo e caminho inexistente; sucesso coberto no emulador | `ros/vipee_bridge/test/v4l2_sink_test.cpp` | `ctest --preset default -L unit` |
| Nó ROS (`CameraBridge`, leitura do mapa) | integration (ROS real no contêiner) | Mapas reais dos dois veículos; `CompressedImage` real publicado; sink de teste (interface própria) registra quadros; só o mais novo; `STALE`/`LIVE`; mapa de câmeras inválido com VHAL intacto | `ros/vipee_bridge/test/*_rostest.cpp` | `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` |
| Plataforma EVS (`android/evs/`, probe, instalação) | integration (emulador) | 4 câmeras listadas e abertas pelo probe; quadros do escritor chegam; fechar câmera sem escritor não trava; `remove` devolve os MP4 | `tools/emu/test-cam.sh` | `tools/emu/test-cam.sh platform` |
| Ponta a ponta (host ➔ emulador) | e2e | Success Criteria do spec; CAM-02, 05, 08, 15, 19..21 | `tools/emu/test-cam.sh`, `tools/emu/test-e2e.sh` | `tools/emu/test-cam.sh` e `tools/emu/test-e2e.sh` |
| Ferramentas de teste (`fake_sim.py`, `extract_frames.py`) | none | exercitadas pelo e2e; `ruff` | - | `ruff check tools/emu` |
| Build/config (CMake, vcpkg, Soong, `.rc`, sepolicy, XML, docs) | none | build gate only | - | build gate only |

## Gate Check Commands

> Generated from codebase - confirm before Execute.

| Gate Level | When to Use | Command |
| ---------- | ----------- | ------- |
| Quick | Tasks só com testes unit | `cmake --build --preset default && ctest --preset default -L unit` |
| Full | Tasks com integração ROS ou emulador | Quick + `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` (ROS) ou `tools/emu/test-cam.sh platform` (emulador) |
| Build | Fim de fase e tasks de config | `cmake --preset default && cmake --build --preset default && ctest --preset default && clang-format --dry-run --Werror <arquivos tocados> && clang-tidy -p build <arquivos tocados>`; da fase 3 em diante também `tools/jazzy/run.sh /bridge/tools/jazzy/build-bridge.sh` |

Builds AOSP só na VM `vipee` por `~/aosp-scripts/vm_m.sh` (nunca dois `m` ao mesmo tempo). Mexer no emulador compartilhado (instalar, reiniciar, trocar a HAL EVS) e cada build na VM exigem o ok do usuário para esta feature; push e `adb` em device físico, idem.

---

## Execution Plan

Dependências entre fases vêm da ordem das fases (T4, T9 e T13 abrem fase e usam tudo que veio antes).

### Phase 1: Plataforma EVS no emulador

Tira primeiro os riscos da HAL e do kernel, com o escritor do spike, antes de qualquer código de produto.

```
T1 → T2 → T3
```

### Phase 2: Núcleo no host

Domínio puro, sem ROS nem Android.

```
T4 → T5 → T6 → T7 → T8
```

### Phase 3: Processo ROS

Build cruzado, mapa, sink e ligação no nó.

```
T9 → T10 → T11 → T12
```

### Phase 4: Ponta a ponta e docs

```
T13 → T14 → T15 → T16
```

---

## Task Breakdown

### T1: Probe EVS nativo

**What**: `vipee_evs_probe`: cliente do `evsmanagerd` (AIDL `IEvsEnumerator`) que abre as câmeras pedidas por N segundos e imprime `PROBE <id> frames=<n> width=<w> height=<h> format=<f> max_gap_ms=<g> last_frame_ms_ago=<t>`; com `--list` lista as câmeras; com `--close-after-ms` fecha o stream no meio e mede quanto o `stopVideoStream` demora.
**Where**: `android/evs_probe/vipee_evs_probe.cpp` (com `Android.bp`)
**Depends on**: None
**Reuses**: `android/vhal/Android.bp` (padrão Soong vendor), `tools/emu/vhal-sync.sh` (sync para a árvore e `vm_m.sh`)
**Requirement**: CAM-02, CAM-19

**Tools**:

- MCP: `context7` (EVS AIDL `IEvsEnumerator`/`IEvsCamera`/`IEvsCameraStream`)
- Skill: NONE

**Done when**:

- [ ] Build na VM (`vm_m.sh vipee_evs_probe`) sem erro, com o ok do usuário
- [ ] No emulador atual (MP4), `vipee_evs_probe --list` mostra `cam_front`..`cam_right` e `vipee_evs_probe 5 cam_front` conta quadros 1280×720
- [ ] Doxygen no topo do arquivo (uso, saída, códigos de saída)

**Tests**: integration
**Gate**: full

**Commit**: `feat(evs): add a headless evs probe that counts frames per camera`

---

### T2: Plataforma EVS por v4l2loopback

**What**: `android/evs/` com `evs_configuration_v4l2.xml` (4 dispositivos 1280×720), `init.vipee_evs.rc` (recarga do `v4l2loopback` com `devices=4 video_nr=10,11,12,13`, `evs-v4l2`, `evsmanagerd --target hw/1`), linhas de `ueventd` (`0660 system camera`); `install-vendor.sh evs` e `remove` com cópias `*.stock`; `set-camera-id` dos 4 tipos; regra `video_device` no `vendor_vipee_ros2.te` e grupo `camera` no `vipee_bridge.rc`. Teste `tools/emu/test-cam.sh platform`: com o escritor do spike (`~/ros2_android_ws/spike_v4l2`) em 1280×720, o probe lista `/dev/video10..13` e recebe quadros 1280×720 das 4; `remove` devolve os MP4.
**Where**: `android/evs/init.vipee_evs.rc` (com o XML e as linhas de `ueventd` ao lado; `tools/jazzy/install-vendor.sh`, `tools/jazzy/sepolicy/vendor_vipee_ros2.te`, `tools/emu/test-cam.sh`)
**Depends on**: T1
**Reuses**: passo do VHAL em `install-vendor.sh`; `sepolicy-sync.sh`; `v4l2_spike.cpp`
**Requirement**: CAM-19, CAM-20, CAM-21

**Tools**:

- MCP: `context7` (v4l2loopback, Android init `exec`/`modprobe`)
- Skill: NONE

**Done when**:

- [ ] Build na VM de `android.hardware.automotive.evs-v4l2` e `selinux_policy`, com o ok do usuário
- [ ] Depois do reboot, `/dev/video10..13` existem com `system camera 0660`; `evs-v4l2` e `evsmanagerd` rodam; nenhum `avc: denied` de `vendor_modprobe`, `hal_evs_default` ou `vendor_vipee_ros2`
- [ ] Se o `modprobe -r` no `.rc` falhar, plano B do design (blocklist + `insmod`) aplicado e anotado
- [ ] `tools/emu/test-cam.sh platform` passa (4 câmeras, 1280×720, quadros; `remove` volta aos MP4)

**Tests**: integration
**Gate**: full

**Commit**: `feat(evs): serve the four cameras from v4l2loopback through evs-v4l2`

---

### T3: Riscos da HAL com escritor parado

**What**: Passo `stall` no `test-cam.sh`: com o escritor parado, o probe não recebe quadro (nunca repete); fechar a câmera com o escritor parado termina em menos de 1 s; abrir a câmera antes de o escritor existir e depois dele dá 1280×720 nos dois casos; escritor volta e os quadros retomam sem reabrir. Resultado anotado no `design.md` (Risks).
**Where**: `tools/emu/test-cam.sh` (passo `stall`)
**Depends on**: T2
**Reuses**: `v4l2_spike.cpp`, probe do T1
**Requirement**: CAM-04, CAM-06, CAM-07

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] `tools/emu/test-cam.sh stall` passa
- [ ] Se o fechamento travar ou a ordem de abertura mudar o tamanho: parar e escalar ao usuário com proposta de AD (não seguir para a fase 2)

**Tests**: integration
**Gate**: full

**Commit**: `test(evs): check the hal with a silent writer`

---

### T4: libjpeg-turbo no build do host

**What**: `libjpeg-turbo` no `vcpkg.json` e ligado ao núcleo no `CMakeLists.txt` (`find_package(libjpeg-turbo CONFIG)`, alvo estático `turbojpeg-static`).
**Where**: `vcpkg.json` (e `CMakeLists.txt`)
**Depends on**: None
**Reuses**: preset `default`
**Requirement**: CAM-01

**Tools**:

- MCP: `context7` (vcpkg port libjpeg-turbo, alvos CMake)
- Skill: NONE

**Done when**:

- [x] `cmake --preset default && cmake --build --preset default` liga com `turbojpeg-static`
- [x] Suíte atual 100% verde (mesma contagem)

**Tests**: none
**Gate**: build

**Commit**: `build(core): add libjpeg-turbo for camera decoding`

---

### T5: Câmeras no mapa

**What**: `CameraConfig`, `EvsView`, `VehicleMap::cameras`, `kMapVersion = 2` e `validateCameras` com `CameraMapError` (`kUnknownView`, `kDuplicateView`, `kDuplicateDevice`, `kMissingField`, `kBadSize`, `kBadStaleMs`); erro de câmera não muda `validateMap`.
**Where**: `core/src/vehicle_map.cpp` (com `core/include/vipee/vehicle_map.h`, `core/test/vehicle_map_test.cpp`)
**Depends on**: T4
**Reuses**: `validateMap`, `gearFromName`
**Requirement**: CAM-16, CAM-17, CAM-18

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Um teste por erro de câmera; lista vazia é válida (CAM-18); mapa de telemetria válido com câmeras inválidas continua `kNone` em `validateMap` (CAM-17)
- [x] Testes de versão do mapa atualizados para 2 (versão 1 recusada)
- [x] Gate quick passa; contagem anterior + novos, sem remoção

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): declare and validate cameras in the vehicle map`

---

### T6: JPEG ➔ YUYV

**What**: `decodeJpegToYuyv` (formato, tamanho pelo cabeçalho antes de decodificar, erro de decodificação) e `packI420ToYuyv`/4:2:2.
**Where**: `core/src/jpeg_yuyv.cpp` (com `core/include/vipee/jpeg_yuyv.h`, `core/test/jpeg_yuyv_test.cpp`)
**Depends on**: T5
**Reuses**: —
**Requirement**: CAM-01, CAM-09, CAM-10, CAM-11

**Tools**:

- MCP: `context7` (TurboJPEG 3: `tj3DecompressHeader`, `tj3DecompressToYUVPlanes8`, `TJPARAM_SUBSAMP`)
- Skill: NONE

**Done when**:

- [x] Testes com JPEG gerado no teste (`tj3Compress8` de um padrão conhecido) 4:2:0 e 4:2:2: Y e croma YUYV dentro de tolerância de compressão; 480×480 ➔ `kFrameSizeMismatch` sem decodificar; `format` `png` ➔ `kFormatUnsupported`; `format` `rgb8; jpeg compressed bgr8` aceito; JPEG cortado ➔ `kDecodeError`; 4:4:4 ➔ `kDecodeError`
- [x] Teste do `packI420ToYuyv` com planos pequenos conhecidos (valores exatos)
- [x] Gate quick passa

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): decode jpeg frames to yuyv`

---

### T7: LatestSlot

**What**: Slot de uma posição: `put` substitui, `take(timeout)` espera, `close` libera.
**Where**: `core/include/vipee/latest_slot.h` (com `core/test/latest_slot_test.cpp`)
**Depends on**: T6
**Reuses**: —
**Requirement**: CAM-23, CAM-24

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Testes: dois `put` antes do `take` entregam só o segundo; `take` vazio volta `nullopt` no timeout; `close` acorda um `take` bloqueado; `put` em sequência sem `take` não acumula (um só valor guardado)
- [x] Gate quick passa

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): add a single-slot latest-value mailbox`

---

### T8: CameraChannel

**What**: `CameraChannel` com `onFrame`, `tick`, `takeStats`, `FrameSink` e `CameraLogger`: escreve cada quadro válido uma vez, `NO_DATA`/`LIVE`/`STALE`, `DROP <motivo>` por mudança, `DEVICE_UNAVAILABLE`/`DEVICE_OK` com reabertura a cada 1 s, latência `now - rx`.
**Where**: `core/src/camera_channel.cpp` (com `core/include/vipee/camera_channel.h`, `core/include/vipee/frame_sink.h`, `core/test/camera_channel_test.cpp`)
**Depends on**: T7
**Reuses**: padrão de estado e `Logger` do `Bridge`
**Requirement**: CAM-01, CAM-03, CAM-04, CAM-06, CAM-07, CAM-12, CAM-13, CAM-14, CAM-22

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Testes com sink e logger fakes: quadro válido escrito uma vez com os bytes YUYV certos; sem quadro novo, `tick` não escreve nada; `STALE` em `stale_ms + 1`, não em `stale_ms`; `LIVE` no quadro seguinte; descarte não renova o prazo; `DROP` logado uma vez por motivo e de novo quando muda; `write` falha ➔ `DEVICE_UNAVAILABLE` uma vez e `open` tentado de novo só depois de 1 s; `open` ok ➔ `DEVICE_OK`; `rx` com `header.stamp` voltando (loop do bag) não muda nada; p95 de latência calculado
- [x] Gate quick passa; build gate da fase 2 (clang-format, clang-tidy nos arquivos tocados)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): add the per-camera channel with stale and drop handling`

---

### T9: sensor_msgs e libjpeg-turbo no build cruzado

**What**: `sensor_msgs` no `colcon --packages-up-to` do Android; libjpeg-turbo 3.1 fixada no `fetch-src.sh` e compilada estática com o NDK (`-z max-page-size=16384`) num passo do `build-android.sh`; `build-bridge.sh` confere que nenhuma lib versionada nova aparece.
**Where**: `tools/jazzy/build-android.sh` (com `tools/jazzy/fetch-src.sh`)
**Depends on**: None
**Reuses**: passo do `sirv_msgs` em `build-android.sh`
**Requirement**: CAM-01

**Tools**:

- MCP: `context7` (libjpeg-turbo CMake, cross-compile Android)
- Skill: NONE

**Done when**:

- [ ] `install_android_x86_64/sensor_msgs` e a `libturbojpeg.a` x86_64 existem
- [ ] `build-bridge.sh` passa sem lib versionada (`llvm-readelf -d`)

**Tests**: none
**Gate**: build

**Commit**: `build(jazzy): cross-build sensor_msgs and libjpeg-turbo`

---

### T10: Câmeras lidas do mapa no nó

**What**: `mapFromOverrides` lê `cameras.<pos>.{evs,topic,device,width,height,stale_ms}`; `maps/commander.yaml` e `maps/cat793f.yaml` em `map_version: 2` com as 4 câmeras; teste ROS confere os dois mapas e um mapa com câmera inválida (`CAM MAP_INVALID`, VHAL intacto).
**Where**: `ros/vipee_bridge/src/bridge_node.cpp` (com `maps/*.yaml`, `ros/vipee_bridge/test/bridge_node_rostest.cpp`)
**Depends on**: T9
**Reuses**: `mapFromOverrides`
**Requirement**: CAM-16, CAM-17, CAM-18

**Tools**:

- MCP: `context7` (rclcpp parâmetros aninhados)
- Skill: NONE

**Done when**:

- [ ] Testes ROS: os dois mapas reais carregam 4 câmeras com os valores do arquivo; mapa sem `cameras` dá lista vazia; posição repetida dá `CAM MAP_INVALID` e as 4 propriedades do VHAL seguem como antes
- [ ] Gate full (ROS) passa

**Tests**: integration
**Gate**: full

**Commit**: `feat(ros): read the cameras from the vehicle map`

---

### T11: V4l2Sink

**What**: `V4l2Sink` (`FrameSink`): `open` com `O_RDWR`, `VIDIOC_S_FMT` de saída YUYV no tamanho do mapa, `write` do quadro inteiro, fecha o fd na falha.
**Where**: `ros/vipee_bridge/src/v4l2_sink.cpp` (com header e `ros/vipee_bridge/test/v4l2_sink_test.cpp`)
**Depends on**: T10
**Reuses**: `v4l2_spike.cpp` (escritor)
**Requirement**: CAM-14

**Tools**:

- MCP: `context7` (V4L2 output, `VIDIOC_S_FMT`)
- Skill: NONE

**Done when**:

- [ ] Testes: caminho inexistente ➔ `open` falso; arquivo comum ➔ `S_FMT` falha e `open` falso, fd fechado; `write` com fd inválido ➔ falso e `isOpen` falso
- [ ] Gate quick passa

**Tests**: unit
**Gate**: quick

**Commit**: `feat(ros): write yuyv frames to a v4l2 output device`

---

### T12: CameraBridge no nó

**What**: `CameraBridge`: subscription `CompressedImage` (`SensorDataQoS`, depth 1) por câmera, `LatestSlot` e thread por câmera, timer de 50 ms (`tick`) e de 5 s (`STATS`); criado pelo `BridgeNode` quando o mapa tem câmeras válidas; `CameraLogger` da plataforma no `main.cpp` (tag `vipee_bridge`).
**Where**: `ros/vipee_bridge/src/camera_bridge.cpp` (com header, `bridge_node.cpp`, `main.cpp`, `CMakeLists.txt`, `ros/vipee_bridge/test/camera_bridge_rostest.cpp`)
**Depends on**: T11
**Reuses**: `BridgeNode`, `PlatformLogger`
**Requirement**: CAM-01, CAM-04, CAM-06, CAM-07, CAM-08, CAM-23

**Tools**:

- MCP: `context7` (rclcpp QoS, subscriptions com `shared_ptr`)
- Skill: NONE

**Done when**:

- [ ] Teste ROS real com sink de teste: JPEG 1280×720 publicado chega ao sink da câmera certa uma vez; publicação parada ➔ `CAM <pos> STALE` e nenhum quadro novo; volta ➔ `LIVE`; rajada de 5 mensagens com sink lento entrega a última; câmera parada não atrasa a telemetria no socket do teste
- [ ] `build-bridge.sh` passa (binário Android liga com `libturbojpeg.a`)
- [ ] Gate full (ROS) passa

**Tests**: integration
**Gate**: full

**Commit**: `feat(ros): bridge the four compressed camera topics to their channels`

---

### T13: Câmeras no fake_sim

**What**: `extract_frames.py` tira um JPEG por câmera do bag; `fake_sim.py` publica as 4 câmeras a 10 Hz conforme `"cameras"` no estado (`ok`, `small`, `png`, `trunc`, `off`), no mesmo nó.
**Where**: `tools/emu/fake_sim.py` (com `tools/emu/extract_frames.py`)
**Depends on**: None
**Reuses**: `fake_sim.py`
**Requirement**: CAM-05, CAM-09, CAM-10, CAM-11

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] No contêiner, `ros2 topic hz` mostra ~10 Hz nas 4 câmeras com `ok` e nada com `off`
- [ ] `ruff check tools/emu` limpo

**Tests**: none
**Gate**: build

**Commit**: `test(e2e): publish the four cameras from the fake simulator`

---

### T14: E2E das câmeras

**What**: Passos `bag`, `stale`, `drops`, `device` e `install` no `test-cam.sh`: bag em loop com `commander` ➔ probe com ≥ 8 quadros/s e 1280×720 nas 4 por 10 s e `STATS p95_ms ≤ 100`; bag parado ➔ sem quadro depois de 600 ms e `STALE` das 4, bag volta ➔ quadros sem reabrir; `fake_sim` com `small`/`png`/`trunc` ➔ `DROP` com o motivo certo e `STALE`; mapa com uma câmera em dispositivo inexistente ➔ `DEVICE_UNAVAILABLE`, outras 3 e VHAL ok; `install`/`remove`.
**Where**: `tools/emu/test-cam.sh`
**Depends on**: T13
**Reuses**: `until_all`, `start_sim` do `test-e2e.sh`
**Requirement**: CAM-02, CAM-03, CAM-06, CAM-07, CAM-09, CAM-10, CAM-11, CAM-12, CAM-14, CAM-15, CAM-19, CAM-20

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] `tools/emu/test-cam.sh` passa inteiro, com o ok do usuário para usar o emulador
- [ ] Zero `avc: denied` nos domínios da ponte, da HAL EVS e do `vendor_modprobe`

**Tests**: e2e
**Gate**: full

**Commit**: `test(e2e): check the four cameras end to end`

---

### T15: Telemetria com câmeras ligadas

**What**: `test-e2e.sh` liga as 4 câmeras do `fake_sim` (`ok`) durante todos os passos e confere, no fim, `STATS` das 4 com quadros.
**Where**: `tools/emu/test-e2e.sh`
**Depends on**: T14
**Reuses**: `test-e2e.sh`
**Requirement**: CAM-05, CAM-08

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] `tools/emu/test-e2e.sh` passa com as câmeras ligadas (latência ≤ 200 ms, dado velho ≤ 600 ms)

**Tests**: e2e
**Gate**: full

**Commit**: `test(e2e): run the telemetry checks with the cameras on`

---

### T16: Contrato e README

**What**: `docs/sim-contract.md` §3.4: 1280×720 de referência (simulador será ajustado), FOV do CAT793F indefinido, Commander ~192°, rig da tabela é o do CAT793F e o bag de calibração é do Commander com os mesmos tópicos; §6 sem a dúvida 720p/480×480. `README.md`: câmeras na seção "Build e execução" (instalar/remover, `set-camera-id` depois do reboot, tocar o bag).
**Where**: `docs/sim-contract.md` (com `README.md`)
**Depends on**: T15
**Reuses**: —
**Requirement**: CAM-19

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] Textos atualizados; versão do contrato sobe (v0.2.3)
- [ ] Página pública do contrato sincronizada só com ok do usuário

**Tests**: none
**Gate**: build

**Commit**: `docs(contract): cameras at 720p and the calibration bag`

---

## Phase Execution Map

```
Phase 1 → Phase 2 → Phase 3 → Phase 4

Phase 1:  T1 → T2 → T3
Phase 2:  T4 → T5 → T6 → T7 → T8
Phase 3:  T9 → T10 → T11 → T12
Phase 4:  T13 → T14 → T15 → T16
```

---

## Task Granularity Check

| Task | Scope | Status |
| ---- | ----- | ------ |
| T1 | 1 binário de teste | ✅ |
| T2 | Arquivos de plataforma de uma mesma troca de HAL (instaláveis só juntos) | ⚠️ coeso |
| T3 | 1 passo de teste | ✅ |
| T4 | 1 dependência de build | ✅ |
| T5 | 1 função de validação + struct | ✅ |
| T6 | 1 módulo (2 funções) | ✅ |
| T7 | 1 classe | ✅ |
| T8 | 1 classe + interface do sink | ✅ |
| T9 | 1 passo de build | ✅ |
| T10 | 1 função + dados do mapa | ✅ |
| T11 | 1 classe | ✅ |
| T12 | 1 classe + ligação | ⚠️ coeso |
| T13 | 1 ferramenta | ✅ |
| T14 | 1 script | ✅ |
| T15 | 1 script | ✅ |
| T16 | docs | ✅ |

## Diagram-Definition Cross-Check

| Task | Depends On (task body) | Diagram Shows | Status |
| ---- | ---------------------- | ------------- | ------ |
| T1 | None | início da fase 1 | ✅ |
| T2 | T1 | T1 → T2 | ✅ |
| T3 | T2 | T2 → T3 | ✅ |
| T4 | None | início da fase 2 | ✅ |
| T5 | T4 | T4 → T5 | ✅ |
| T6 | T5 | T5 → T6 | ✅ |
| T7 | T6 | T6 → T7 | ✅ |
| T8 | T7 | T7 → T8 | ✅ |
| T9 | None (ordem de fase) | início da fase 3 | ✅ |
| T10 | T9 | T9 → T10 | ✅ |
| T11 | T10 | T10 → T11 | ✅ |
| T12 | T11 | T11 → T12 | ✅ |
| T13 | None (ordem de fase) | início da fase 4 | ✅ |
| T14 | T13 | T13 → T14 | ✅ |
| T15 | T14 | T14 → T15 | ✅ |
| T16 | T15 | T15 → T16 | ✅ |

## Test Co-location Validation

| Task | Code Layer Created/Modified | Matrix Requires | Task Says | Status |
| ---- | --------------------------- | --------------- | --------- | ------ |
| T1 | Plataforma EVS (probe) | integration | integration | ✅ |
| T2 | Plataforma EVS | integration | integration | ✅ |
| T3 | Plataforma EVS | integration | integration | ✅ |
| T4 | Build/config | none | none | ✅ |
| T5 | Núcleo | unit | unit | ✅ |
| T6 | Núcleo | unit | unit | ✅ |
| T7 | Núcleo | unit | unit | ✅ |
| T8 | Núcleo | unit | unit | ✅ |
| T9 | Build/config | none | none | ✅ |
| T10 | Nó ROS | integration | integration | ✅ |
| T11 | `V4l2Sink` | unit | unit | ✅ |
| T12 | Nó ROS | integration | integration | ✅ |
| T13 | Ferramentas de teste | none | none | ✅ |
| T14 | Ponta a ponta | e2e | e2e | ✅ |
| T15 | Ponta a ponta | e2e | e2e | ✅ |
| T16 | Docs | none | none | ✅ |
