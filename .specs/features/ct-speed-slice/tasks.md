# ct-speed-slice Tasks

## Execution Protocol (MANDATORY -- do not skip)

Implement these tasks with the `tlc-spec-driven` skill: **activate it by name and follow its Execute flow and Critical Rules.** Do not search for skill files by filesystem path. The skill is the source of truth for the full flow (per-task cycle, sub-agent delegation, adequacy review, Verifier, discrimination sensor).

**If the skill cannot be activated, STOP and tell the user - do not proceed without it.**

---

**Design**: `.specs/features/ct-speed-slice/design.md`
**Status**: Approved

---

## Test Coverage Matrix

> Generated from codebase, project guidelines, and spec - confirm before Execute. Guidelines found: `AGENTS.md` §3 (RED-GREEN-REFACTOR, núcleo sem ROS nem Android com fakes da interface própria, integração marcada e separada, fail-loud), §3.1 (clang-format/clang-tidy por arquivo inteiro), `../VIPEE-CalibDroid/CMakePresets.json` e `CMakeLists.txt` (GoogleTest via vcpkg, `gtest_discover_tests` com `LABELS`). O repo ainda não tem testes.

| Code Layer | Required Test Type | Coverage Expectation | Location Pattern | Run Command |
| ---------- | ------------------ | -------------------- | ---------------- | ----------- |
| Núcleo (`core/`: mapa, tradução, `Bridge`, protocolo) | unit | Todos os ramos; 1:1 com CTS-01..CTS-09 e CTS-11..CTS-23; todos os edge cases do spec | `core/test/*_test.cpp` | `ctest --preset default -L unit` |
| `SocketSink` (POSIX, sem ROS) | unit | Envio, reconexão, reenvio do estado, heartbeat; com `socketpair`/socket temporário | `ros/vipee_bridge/test/socket_sink_test.cpp` | `ctest --preset default -L unit` |
| Nó ROS (`BridgeNode`, leitura do mapa) | integration (ROS real no contêiner) | Mapas reais dos dois veículos, mensagens `sirv_msgs` reais publicadas, registros lidos de um socket do teste; mapa inválido | `ros/vipee_bridge/test/*_rostest.cpp` | `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` |
| VHAL (`android/vhal/`) | integration (emulador) | Boot com as 4 propriedades; `get` com `NOT_AVAILABLE`/`INTERNAL_ERROR`/valor; queda do socket; watchdog; taxa da contínua | `tools/emu/test-*.sh` | `tools/emu/test-vhal.sh` |
| Ponta a ponta (host ➔ emulador) | e2e | Os 3 Success Criteria do spec | `tools/emu/test-e2e.sh` | `tools/emu/test-e2e.sh` |
| Build/config (CMake, Soong, `.rc`, sepolicy, XML Cyclone, scripts de build) | none | build gate only | - | build gate only |

## Gate Check Commands

> Generated from codebase - confirm before Execute.

| Gate Level | When to Use | Command |
| ---------- | ----------- | ------- |
| Quick | Tasks só com testes unit | `cmake --build --preset default && ctest --preset default -L unit` |
| Full | Tasks com integração ROS ou emulador | Quick + `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` (ROS) ou `tools/emu/test-vhal.sh` (emulador) |
| Build | Fim de fase e tasks de config | `cmake --preset default && cmake --build --preset default && ctest --preset default && clang-format --dry-run --Werror <arquivos tocados> && clang-tidy -p build <arquivos tocados>`; na fase 2 também `tools/jazzy/run.sh /bridge/tools/jazzy/build-bridge.sh` |

Builds no `~/aosp` (`m vipee-vehicle-service`, `m selinux_policy`) exigem aprovação explícita antes e `df -h /mnt/c` (AGENTS.md §5, §6.2). Push/`adb` em device físico, idem.

---

## Execution Plan

Dependências entre fases vêm da ordem das fases (T8, T13 e T17 abrem fase e usam tudo que veio antes).

### Phase 1: Núcleo no host

Scaffolding e domínio puro, sem ROS nem Android.

```
T1 → T2 → T3 → T4 → T5 → T6 → T7
```

### Phase 2: Processo ROS

Mensagens do simulador, sink do socket e nó, no contêiner Jazzy.

```
T8 → T9 → T10 → T11 → T12
```

### Phase 3: VHAL no emulador

Esqueleto primeiro (descobre cedo o efeito de tirar o VHAL fake), depois o socket e a política.

```
T13 → T14 → T15 → T16
```

### Phase 4: Ponta a ponta

```
T17 → T18
```

---

## Task Breakdown

### T1: Scaffolding CMake + vcpkg + GoogleTest

**What**: `CMakeLists.txt` raiz, `CMakePresets.json` e `vcpkg.json` (gtest) espelhando o CalibDroid, com a biblioteca `vipee_core` vazia e um teste de fumaça.
**Where**: `CMakeLists.txt`
**Depends on**: None
**Reuses**: `../VIPEE-CalibDroid/CMakePresets.json`, `vcpkg.json`, `CMakeLists.txt` (`gtest_discover_tests … LABELS unit`)
**Requirement**: CTS-20

**Tools**:

- MCP: `context7` (CMake, vcpkg)
- Skill: NONE

**Done when**:

- [x] `cmake --preset default && cmake --build --preset default && ctest --preset default` passa com 1 teste de fumaça
- [x] `build/compile_commands.json` gerado (pré-requisito do clang-tidy)

**Tests**: none
**Gate**: build

**Commit**: `build(core): cmake, vcpkg and gtest scaffolding`

---

### T2: Protocolo do socket (`wire.h`)

**What**: `WireRecord` de 32 bytes, `kWireVersion = 1`, `kind` update/heartbeat, com `static_assert` de tamanho e offsets e funções `encode`/`decode` que rejeitam versão desconhecida.
**Where**: `core/include/vipee/wire.h`
**Depends on**: T1
**Reuses**: design §Protocolo do socket
**Requirement**: CTS-09

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Ida e volta preserva todos os campos; versão diferente devolve erro nomeado
- [x] Doxygen com unidades (m/s, ns de `CLOCK_BOOTTIME`)
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 3 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): versioned 32-byte wire record`

---

### T3: Mapa do veículo e validação

**What**: `VehicleMap` (versão, veículo, namespace, `stale_ms`, escala, `max_abs_kmh`, fonte de `GEAR_SELECTION` com tabela da alavanca) e `validateMap` que recusa versão desconhecida, campo faltando, tabela inconsistente e propriedade fora das 4.
**Where**: `core/include/vipee/vehicle_map.h`
**Depends on**: T2
**Reuses**: design §Data Models
**Requirement**: CTS-20, CTS-23

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Mapas equivalentes ao `cat793f` e ao `commander` do design validam
- [x] Cada motivo de recusa tem teste próprio (versão, `stale_ms ≤ 0`, `max_abs_kmh ≤ 0`, códigos e marchas da alavanca com tamanhos diferentes, código repetido, marcha desconhecida)
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 8 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): vehicle map model and validation`

---

### T4: Funções de tradução

**What**: funções puras `speedKmhToMps`, `currentGearFromEngaged`, `derivedSelectionFromEngaged` e `selectionFromLever`, cada uma devolvendo valor ou motivo (`OUT_OF_RANGE`, `UNKNOWN_GEAR`).
**Where**: `core/include/vipee/translate.h`
**Depends on**: T3
**Reuses**: valores `VehicleGear` do `VehicleGear.aidl` (AOSP 16) como constantes próprias
**Requirement**: CTS-01, CTS-02, CTS-03, CTS-04, CTS-05, CTS-06, CTS-16, CTS-17

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] 36 ➔ 10.0; -18 ➔ -5.0; -0.0 ➔ 0.0; 80.0 aceito e 80.1 recusado no limite 80; `NaN` e `inf` recusados
- [x] `gear` -2, -1, 0, 1..9 pela tabela genérica; -10 e 10 ➔ `UNKNOWN_GEAR`
- [x] Derivada nunca devolve `GEAR_PARK`; alavanca: os 6 códigos e um desconhecido
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 14 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): speed and gear translation`

---

### T5: `Bridge` com telemetria, dado velho e log

**What**: classe `Bridge` com `PropertySink`, `Logger`, `onTelemetry` e `tick`: publica `PERF_VEHICLE_SPEED`, `CURRENT_GEAR` e `GEAR_SELECTION` derivada; estado inicial `NO_DATA`; `STALE` após `stale_ms`; volta a `AVAILABLE`; sem evento repetido nas ON_CHANGE; carimbo = `now_ns` da chegada; mapa inválido deixa as 4 `UNAVAILABLE`/`MAP_INVALID`.
**Where**: `core/src/bridge.cpp`
**Depends on**: T4
**Reuses**: T2–T4
**Requirement**: CTS-01, CTS-02, CTS-03, CTS-05, CTS-06, CTS-08, CTS-09, CTS-11, CTS-12, CTS-15, CTS-16, CTS-17, CTS-19, CTS-23

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Testes com sink e logger fakes e relógio fake, um por AC listado
- [x] Velocidade `ERROR` não afeta as marchas; marcha `UNKNOWN_GEAR` não afeta a velocidade
- [x] Cada transição de status gera exatamente uma entrada de log com o motivo
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 16 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): bridge state machine for telemetry`

---

### T6: Alavanca do CAT793F no `Bridge`

**What**: `onLever`: com fonte `lever`, `GEAR_SELECTION` vem só da alavanca, com validade própria; com fonte `derived`, a alavanca é ignorada.
**Where**: `core/src/bridge.cpp` (modify)
**Depends on**: T5
**Reuses**: `selectionFromLever` (T4)
**Requirement**: CTS-04, CTS-14, CTS-17, CTS-22

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `'P'` ➔ `GEAR_PARK`; alavanca velha ➔ `GEAR_SELECTION` `STALE` com telemetria fresca; código desconhecido ➔ `UNKNOWN_GEAR`
- [x] Mesma telemetria (`gear = 0`): mapa CAT793F com `'P'` dá `GEAR_PARK`, mapa Commander dá `GEAR_NEUTRAL`
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 6 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): cat793f lever as gear selection source`

---

### T7: Freio de mão e fonte ambígua no `Bridge`

**What**: `onControl`: `PARKING_BRAKE_ON = handbrake` de qualquer `id`; dois `id` diferentes dentro de `stale_ms` ➔ `UNAVAILABLE`/`AMBIGUOUS_SOURCE` até um `stale_ms` inteiro com um só `id`; controle velho ➔ `STALE`.
**Where**: `core/src/bridge.cpp` (modify)
**Depends on**: T6
**Reuses**: T5
**Requirement**: CTS-07, CTS-08, CTS-13, CTS-18, CTS-19

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `handbrake` true/false; mesmo valor repetido não gera evento; troca de `id` com pausa > `stale_ms` não é ambígua
- [x] Controle velho não afeta as propriedades da telemetria
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 6 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(core): parking brake with ambiguous source detection`

---

### T8: `sirv_msgs` no build Android

**What**: `build-android.sh` passa a compilar `diagnostic_msgs` e o `sirv_msgs` (base path configurável, padrão `../SIRV-Simulator/sirv_ros_pkgs/sirv_msgs` montado no contêiner), como no spike.
**Where**: `tools/jazzy/build-android.sh`
**Depends on**: None
**Reuses**: `~/ros2_android_ws/spike_sirv.sh`
**Requirement**: CTS-01

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Build no contêiner termina com `sirv_msgs` instalado
- [x] `llvm-readelf -d` sem `.so.N` de fora do install; `LOAD` alinhado a `0x4000`

**Tests**: none
**Gate**: build

**Commit**: `build(jazzy): cross-build sirv_msgs and diagnostic_msgs`

---

### T9: Peers Cyclone do simulador no device

**What**: `cyclonedds_device.xml` disca também para as portas do simulador (UE5 7500, vehiclesim 7501) além da 7400.
**Where**: `tools/jazzy/config/cyclonedds_device.xml`
**Depends on**: T8
**Reuses**: contrato v0.2.2 §2
**Requirement**: CTS-10

**Tools**:

- MCP: `context7` (CycloneDDS TCP peers)
- Skill: NONE

**Done when**:

- [x] XML valida no Cyclone (o `vipee_twist_listener` sobe e recebe do host na 7400 como antes)

**Tests**: none
**Gate**: build

**Commit**: `build(jazzy): dial simulator tcp ports from the device`

---

### T10: `SocketSink`

**What**: `SocketSink : PropertySink` sobre `SOCK_SEQPACKET`: conecta ao caminho dado, reconecta a cada 500 ms, heartbeat a cada 100 ms, reenvia o estado atual ao reconectar; não bloqueia o executor.
**Where**: `ros/vipee_bridge/src/socket_sink.cpp`
**Depends on**: T9
**Reuses**: `wire.h` (T2)
**Requirement**: CTS-09, CTS-10

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Teste com servidor `SOCK_SEQPACKET` num diretório temporário: recebe updates, heartbeat, estado completo após reconexão; servidor ausente não trava nem perde o último estado
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: ≥ 4 tests pass (no silent deletions)

**Tests**: unit
**Gate**: quick

**Commit**: `feat(ros): seqpacket socket sink with heartbeat and resync`

---

### T11: `BridgeNode`, mapas e teste ROS

**What**: `main.cpp` do `vipee_bridge` (assinaturas, timer de 50 ms, `CLOCK_BOOTTIME`, leitura dos parâmetros para `VehicleMap`, logger para logcat no Android e stderr no host), `maps/cat793f.yaml`, `maps/commander.yaml` e `tools/jazzy/test-ros.sh` (build host Jazzy + teste de integração).
**Where**: `ros/vipee_bridge/src/main.cpp`
**Depends on**: T10
**Reuses**: `tools/jazzy/bringup/twist_listener.cpp`, `CMakeLists.txt`
**Requirement**: CTS-19, CTS-20, CTS-21, CTS-22, CTS-23

**Tools**:

- MCP: `context7` (rclcpp parâmetros, `SingleThreadedExecutor`)
- Skill: NONE

**Done when**:

- [x] Teste de integração no contêiner: com cada mapa, publica `SirvVehicleTelemetry`, `CAT793FVehicleControl` e `SirvVehicleControl` reais e confere os registros no socket do teste; mapa com parâmetro faltando ➔ 4 `UNAVAILABLE` e `MAP_INVALID`, processo vivo
- [x] Gate check passes: Quick + `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh`
- [x] Test count: ≥ 4 tests pass (no silent deletions)

**Tests**: integration
**Gate**: full

**Commit**: `feat(ros): vipee_bridge node with per-vehicle maps`

---

### T12: Cross-build e serviço vendor do `vipee_bridge`

**What**: `build-bridge.sh` (NDK, `c++_static`, `max-page-size=16384`), `vipee_bridge.rc` (domínio `vendor_vipee_ros2`, mapa por propriedade de sistema) e `install-vendor.sh` instalando binário, libs e mapas no emulador no lugar do `vipee_twist_listener`.
**Where**: `tools/jazzy/build-bridge.sh`
**Depends on**: T11
**Reuses**: `tools/jazzy/build-bringup.sh`, `install-vendor.sh`, `vendor/vipee_twist_listener.rc`
**Requirement**: CTS-21

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] Binário NDK sem `.so.N` de fora; no emulador o serviço sobe em Enforcing e loga `NO_DATA` das 4 com o VHAL ainda ausente

**Tests**: none
**Gate**: build

**Commit**: `build(jazzy): cross-build and install vipee_bridge vendor service`

---

### T13: VHAL esqueleto

**What**: `vipee-vehicle-service`: `VipeeVehicleHardware` com as 4 configs do `vipee_properties.json`, todas `NOT_AVAILABLE`, `main` do serviço, `Android.bp`, `.rc`, VINTF, script de sync para `device/generic/car/emulator/vipee_vhal/` e `tools/emu/test-vhal.sh` (boot, `dumpsys car_service`, driving state, `get` das 4).
**Where**: `android/vhal/VipeeVehicleHardware.cpp`
**Depends on**: None
**Reuses**: `EmulatedVehicleService.cpp`, `JsonConfigLoader`, `DefaultVehicleHal`
**Requirement**: CTS-11

**Tools**:

- MCP: `context7` (AAOS VHAL AIDL, `IVehicleHardware`)
- Skill: NONE

**Done when**:

- [ ] Com aprovação: `m vipee-vehicle-service`; binário instalado por push no caminho do `V4-emulator-service`
- [ ] Emulador dá boot; `CarService` sobe sem as propriedades de plataforma (AD-014) ou a tabela da AD-014 é criada com o que faltar
- [ ] `get` das 4 devolve `NOT_AVAILABLE`
- [ ] Gate check passes: `tools/emu/test-vhal.sh`

**Tests**: integration
**Gate**: full

**Commit**: `feat(vhal): skeleton vehicle hardware with four properties`

---

### T14: Socket, store e watchdog no VHAL

**What**: thread de leitura do `/dev/socket/vipee_vhal` (`android_get_control_socket`), grava em `VehiclePropertyStore` com `ON_VALUE_CHANGE`, `getValues` mapeia status para `NOT_AVAILABLE`/`INTERNAL_ERROR`, socket fechado ou 300 ms mudo ➔ 4 `UNAVAILABLE`, versão desconhecida descartada com log.
**Where**: `android/vhal/VipeeVehicleHardware.cpp` (modify)
**Depends on**: T13
**Reuses**: `VehiclePropertyStore`, `wire.h`
**Requirement**: CTS-08, CTS-09, CTS-11, CTS-15

**Tools**:

- MCP: `context7` (AAOS VHAL)
- Skill: NONE

**Done when**:

- [ ] `test-vhal.sh` com o `vipee_bridge` real e telemetria fake: valor lido; parar o processo ROS ➔ `NOT_AVAILABLE` em ≤ 300 ms
- [ ] Gate check passes: `tools/emu/test-vhal.sh`

**Tests**: integration
**Gate**: full

**Commit**: `feat(vhal): socket ingest, property store and watchdog`

---

### T15: Taxa da propriedade contínua

**What**: `subscribe`/`updateSampleRate` guardam a taxa de `PERF_VEHICLE_SPEED` e o VHAL dizima os eventos para ela.
**Where**: `android/vhal/VipeeVehicleHardware.cpp` (modify)
**Depends on**: T14
**Reuses**: `IVehicleHardware::subscribe`
**Requirement**: CTS-10

**Tools**:

- MCP: `context7` (AAOS VHAL subscribe)
- Skill: NONE

**Done when**:

- [ ] Assinatura a 10 Hz com telemetria a ~66 Hz entrega 10 ± 1 eventos/s
- [ ] Gate check passes: `tools/emu/test-vhal.sh`

**Tests**: integration
**Gate**: full

**Commit**: `feat(vhal): decimate continuous speed events to subscribed rate`

---

### T16: Política SELinux do socket

**What**: tipo `vipee_vhal_socket`, `file_contexts` de `/dev/socket/vipee_vhal`, `unix_socket_connect(vendor_vipee_ros2, vipee_vhal, hal_vehicle_default)` e `dontaudit` de `proc_net`; um único `m selinux_policy`.
**Where**: `tools/jazzy/sepolicy/vendor_vipee_ros2.te`
**Depends on**: T15
**Reuses**: `tools/jazzy/sepolicy/`, `sepolicy-sync.sh`
**Requirement**: CTS-11

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] Com aprovação e `df -h /mnt/c`: `m selinux_policy`
- [ ] Em Enforcing, `test-vhal.sh` passa e não há `avc: denied` dos dois domínios

**Tests**: none
**Gate**: build

**Commit**: `feat(sepolicy): vipee_vhal socket between ros2 and vhal domains`

---

### T17: Publicador fake e teste ponta a ponta

**What**: publicador fake de `sirv_msgs` no contêiner (host ➔ emulador) e `tools/emu/test-e2e.sh` com os 3 Success Criteria: 10 leituras em ≤ 200 ms, parada ➔ `UNAVAILABLE` em ≤ 600 ms com `STALE` no logcat, CAT793F vs Commander sem recompilar.
**Where**: `tools/emu/test-e2e.sh`
**Depends on**: None
**Reuses**: `tools/jazzy/run.sh`, `test-vhal.sh`
**Requirement**: CTS-10, CTS-12, CTS-21, CTS-22

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] `test-e2e.sh` passa nos 3 critérios com os dois mapas
- [ ] Latência medida registrada no `validation.md` (ajusta o default de 200 ms se preciso)

**Tests**: e2e
**Gate**: full

**Commit**: `test(e2e): host to emulator vhal speed slice`

---

### T18: Documentação de uso

**What**: README com build, instalação no emulador e troca de veículo; spec com traceabilidade atualizada.
**Where**: `README.md`
**Depends on**: T17
**Reuses**: AGENTS.md §6.3
**Requirement**: CTS-21

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] Comandos do README rodam como escritos num shell limpo

**Tests**: none
**Gate**: build

**Commit**: `docs(readme): build and run the speed slice`

---

## Phase Execution Map

```
Phase 1 → Phase 2 → Phase 3 → Phase 4

Phase 1:  T1 → T2 → T3 → T4 → T5 → T6 → T7
Phase 2:  T8 → T9 → T10 → T11 → T12
Phase 3:  T13 → T14 → T15 → T16
Phase 4:  T17 → T18
```
