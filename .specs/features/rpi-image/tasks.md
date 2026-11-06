# rpi-image Tasks

## Execution Protocol (MANDATORY -- do not skip)

Implement these tasks with the `tlc-spec-driven` skill: **activate it by name and follow its Execute flow and Critical Rules.** Do not search for skill files by filesystem path. The skill is the source of truth for the full flow (per-task cycle, sub-agent delegation, adequacy review, Verifier, discrimination sensor).

**If the skill cannot be activated, STOP and tell the user - do not proceed without it.**

---

**Design**: `.specs/features/rpi-image/design.md`
**Status**: Draft

---

## Test Coverage Matrix

> Generated from codebase, project guidelines, and spec - confirm before Execute. Guidelines found: `AGENTS.md` §3 (RED-GREEN-REFACTOR, núcleo sem ROS nem Android, nunca mockar `rclcpp` nem HAL, integração marcada e separada, fail-loud) e §3.1 (clang-format/clang-tidy por arquivo inteiro, `ruff` para Python); amostras: `core/test/*_test.cpp`, `ros/vipee_bridge/test/*_rostest.cpp`, `tools/emu/test-*.sh`.

| Code Layer | Required Test Type | Coverage Expectation | Location Pattern | Run Command |
| ---------- | ------------------ | -------------------- | ---------------- | ----------- |
| Núcleo (`core/`: decisão do endereço do host) | unit | Todos os ramos; 1:1 com RPI-05 e o edge case de host inválido | `core/test/*_test.cpp` | `ctest --preset default -L unit` |
| `main.cpp` da ponte (Android) | integration (emulador) | Propriedade definida disca o host; vazia loga `DDS_PEER_UNSET` e não reinicia; XML sem variável segue como hoje | `tools/emu/test-e2e.sh` (passo novo) | `tools/emu/test-e2e.sh peer` |
| Gerador `ros_prebuilts.bp` (Python) | unit (`unittest` da stdlib) | Uma lib por módulo; binário com `init_rc`; toda `NEEDED` resolvida na lista ou no sistema, senão erro nomeado | `tools/rpi/test_gen_prebuilts.py` | `python3 -m unittest tools/rpi/test_gen_prebuilts.py` |
| Câmeras renumeradas (AD-021) | integration (emulador) | `test-cam.sh` inteiro verde com `/dev/video40..43` | `tools/emu/test-cam.sh` | `tools/emu/test-cam.sh` |
| `v4l2loopback.ko` | integration (estática) | `modinfo` com `vermagic` igual ao de um módulo do kernel pré-compilado da placa | `tools/rpi/build-kernel-module.sh` | o próprio script |
| Imagem (`.img`, `vendor.img`) | integration (estática) | Arquivos, rótulos SELinux, `NEEDED` sem `.so.N` externo, `cmdline.txt` sem permissive, `.ko` presente | `tools/rpi/check-image.sh` | `tools/rpi/check-image.sh rpi4` |
| Na placa (RPI-02, 03, 06..12) | e2e (manual, Wendell, pelo handoff) | Passos de conferência do handoff com saída esperada | `Refs/handoff-rpi-image.md` | roteiro do handoff |
| Build/config (toolchain, scripts de build, Soong, `.mk`, `.rc`, sepolicy, XML, docs) | none | build gate only | - | build gate only |

## Gate Check Commands

> Generated from codebase - confirm before Execute.

| Gate Level | When to Use | Command |
| ---------- | ----------- | ------- |
| Quick | Tasks só com testes unit | `cmake --build --preset default && ctest --preset default -L unit` (C++) ou `python3 -m unittest tools/rpi/test_gen_prebuilts.py && ruff check tools/rpi` (Python) |
| Full | Tasks com integração | Quick + o comando da camada (emulador, `modinfo`, `check-image.sh`) |
| Build | Fim de fase e tasks de config | `cmake --preset default && cmake --build --preset default && ctest --preset default && clang-format --dry-run --Werror <arquivos tocados> && clang-tidy -p build <arquivos tocados>`; com código da ponte tocado, também `tools/jazzy/run.sh /bridge/tools/jazzy/build-bridge.sh` (x86_64) e `ABI=arm64 ...build-bridge.sh` |

`m` na VM: um por vez na VM inteira, avisando AVM e CalibDroid antes (SHARED.md). Emulador compartilhado: combinar a janela antes (T6, T9). A aprovação destas tasks autoriza os builds na VM desta feature; push, gravação de SD e `adb` na placa seguem exigindo ok explícito.

---

## Execution Plan

Dependências entre fases vêm da ordem das fases.

### Phase 1: Build arm64 (WSL)

```
T1 → T2 → T3
```

### Phase 2: Endereço do host

```
T4 → T5 → T6
```

### Phase 3: Câmeras em `/dev/video40..43` (AD-021)

```
T7 → T8 → T9
```

### Phase 4: Camada vendor

```
T10 → T11 → T12 → T13 → T14 → T15
```

### Phase 5: Módulo de kernel

```
T16
```

### Phase 6: Imagens e entrega

```
T17 → T18 → T19 → T20
```

---

## Task Breakdown

### T1: Toolchain CMake arm64

**What**: Arquivo de toolchain do NDK para `arm64-v8a`, igual ao x86_64 mais `-Wl,-z,max-page-size=16384`.
**Where**: `tools/jazzy/android_arm64.toolchain.cmake`
**Depends on**: None
**Reuses**: `tools/jazzy/android_x86_64.toolchain.cmake`
**Requirement**: RPI-04

**Tools**:

- MCP: `context7` (NDK/CMake toolchain)
- Skill: NONE

**Done when**:

- [x] `cmake` configura um projeto vazio com o arquivo no contêiner Jazzy
- [x] Gate check passes: build gate (sem código C++ tocado: só a configuração no contêiner)

**Tests**: none
**Gate**: build

---

### T2: ABI no `build-android.sh`

**What**: `ABI=x86_64|arm64` (padrão `x86_64`) escolhe toolchain, diretórios `*_<abi>` e a `libc++_shared` certa; roda o build ROS arm64 inteiro.
**Where**: `tools/jazzy/build-android.sh`
**Depends on**: T1
**Reuses**: o próprio script
**Requirement**: RPI-04

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `ABI=arm64 tools/jazzy/run.sh /bridge/tools/jazzy/build-android.sh` termina e gera `install_android_arm64`
- [x] Sem `ABI`, os caminhos e o resultado são os de antes (diff do script mostra só a parametrização)
- [x] `file` de uma lib mostra `ARM aarch64`

**Tests**: none
**Gate**: build

---

### T3: ABI no `build-bridge.sh` e checagem de `NEEDED`

**What**: Mesmo parâmetro no `build-bridge.sh` (`bridge_build_<abi>`, `push_<abi>/lib`), com a checagem existente de `.so.N` aplicada às duas ABIs.
**Where**: `tools/jazzy/build-bridge.sh`
**Depends on**: T2
**Reuses**: checagem `llvm-readelf -d` do script
**Requirement**: RPI-04

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `ABI=arm64 ...build-bridge.sh` gera `push_arm64/lib` e `bridge_build_arm64/vipee_bridge`
- [x] Nenhuma `NEEDED` versionada de fora do install Android nas duas ABIs
- [x] Build x86_64 sem mudança de saída

**Tests**: none
**Gate**: build

---

### T4: Decisão do endereço do host no núcleo

**What**: Função pura que, dados "o XML usa `${VIPEE_HOST}`" e o valor da propriedade, decide: iniciar sem mexer no ambiente, iniciar com `VIPEE_HOST=<valor>`, ou não iniciar com `DDS_PEER_UNSET` (vazio) / `DDS_PEER_INVALID` (espaço, `:` ou caractere fora de nome/IPv4).
**Where**: `core/include/vipee/dds_peer.h` (+ `core/src/dds_peer.cpp`, `core/test/dds_peer_test.cpp`)
**Depends on**: None
**Reuses**: padrão de erro nomeado do núcleo
**Requirement**: RPI-05, RPI-06

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Testes RED antes do código, 1:1 com os ramos (sem variável; com variável e valor válido; vazio; inválido)
- [x] Gate check passes: `cmake --build --preset default && ctest --preset default -L unit`
- [x] Test count: suíte unit anterior + os novos, todos verdes

**Tests**: unit
**Gate**: quick

---

### T5: Propriedade do host no `main.cpp`

**What**: No Android, lê `persist.vendor.vipee.host`, lê o XML de `CYCLONEDDS_URI`, aplica a decisão de T4: exporta `VIPEE_HOST` ou loga o erro e fica parado (sem sair, sem DDS).
**Where**: `ros/vipee_bridge/src/main.cpp`
**Depends on**: T4
**Reuses**: `logError`, `PlatformLogger`
**Requirement**: RPI-05, RPI-06

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Build x86_64 e arm64 da ponte passam
- [x] Passo `peer` do T6 verde no emulador

**Tests**: integration
**Gate**: full

---

### T6: XML Cyclone da placa e passo `peer` no e2e

**What**: `cyclonedds_rpi.xml` com `<Peer Address="${VIPEE_HOST}:7400"/>`; passo `peer` no `test-e2e.sh` que, no emulador, troca o XML pelo da placa, testa propriedade vazia (`DDS_PEER_UNSET`, processo vivo, sem reinício), `10.0.2.2` (telemetria chega) e restaura o XML original.
**Where**: `tools/emu/test-e2e.sh` (+ `tools/jazzy/config/cyclonedds_rpi.xml`)
**Depends on**: T5
**Reuses**: passos e helpers do `test-e2e.sh`, `fake_sim`
**Requirement**: RPI-05, RPI-06

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Janela no emulador combinada com AVM e CalibDroid
- [x] `tools/emu/test-e2e.sh peer` verde e o `test-e2e.sh` completo continua verde
- [x] XML original de volta no emulador ao fim

**Tests**: integration
**Gate**: full

---

### T7: Mapas com `/dev/video40..43`

**What**: Os dois mapas de veículo apontam as câmeras para `/dev/video40..43` (AD-021).
**Where**: `maps/cat793f.yaml`, `maps/commander.yaml`
**Depends on**: None
**Reuses**: -
**Requirement**: RPI-10

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Teste ROS de mapas (`test-ros.sh`) verde
- [x] Comentário do `vehicle_map.h` atualizado

**Tests**: none
**Gate**: build

---

### T8: Plataforma EVS com `/dev/video40..43`

**What**: `v4l2loopback` com `video_nr=40,41,42,43`, config EVS, `ueventd` e RRO nos números novos.
**Where**: `android/evs/` (config, `.rc`, `ueventd`, RRO; um conjunto coeso)
**Depends on**: T7
**Reuses**: arquivos atuais
**Requirement**: RPI-10

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `grep -rn "video1[0-3]" android/ maps/` sem resultado
- [x] Build na VM (`vm_m.sh VipeeEvsCameraOverlay`) passa, com aviso prévio

**Tests**: none
**Gate**: build

---

### T9: Testes e instalação com os números novos

**What**: `test-cam.sh`, `install-vendor.sh` e README nos números novos; reinstala o EVS no emulador e roda o `test-cam.sh` inteiro.
**Where**: `tools/emu/test-cam.sh` (+ `tools/jazzy/install-vendor.sh`, `README.md`)
**Depends on**: T8
**Reuses**: passos atuais
**Requirement**: RPI-10, RPI-11

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Janela no emulador combinada; `install-vendor.sh evs` aplicado
- [x] `tools/emu/test-cam.sh` inteiro verde
- [x] SHARED.md atualizado (câmeras em `/dev/video40..43`) e AVM e CalibDroid avisados

**Tests**: integration
**Gate**: full

---

### T10: Política SELinux da placa

**What**: Contexto e tipo da propriedade `persist.vendor.vipee.host` (`vendor_vipee_prop`), leitura pela ponte, rótulo `hal_vehicle_default_exec` para `/vendor/bin/hw/vipee-vehicle-service` e o que o `.rc` da placa pedir para carregar o `.ko`.
**Where**: `tools/jazzy/sepolicy/` (arquivos de política; um conjunto coeso)
**Depends on**: None
**Reuses**: política atual
**Requirement**: RPI-02, RPI-03

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] `vm_m.sh selinux_policy` da árvore do emulador passa (a política continua válida lá) — pendente: VM
- [x] Nenhuma regra `permissive` nem `allow` com curinga

**Status**: [x] checado na VM em 2026-11-05: `vm_m.sh selinux_policy vipee-vehicle-service vipee-vehicle-service-rpi` em `~/aosp` OK; em `~/aosp-rpi`, `m nothing` + `m vipee-vehicle-service-rpi vipee-evs-v4l2 vipee_bridge selinux_policy` OK para `aosp_rpi4_car` e `aosp_rpi5_car` (`vendor_vipee_prop` no `vendor_property_contexts`; antes: expansão m4 com os `te_macros` do AOSP 16 local OK; `sepolicy-sync.sh` já copia o `property_contexts` novo)

**Tests**: none
**Gate**: build

---

### T11: Alvo `vipee-vehicle-service-rpi`

**What**: Segundo `cc_binary` do VHAL da ponte, com `.rc` próprio (caminho em `/vendor/bin/hw`, socket `vipee_vhal`), fragmento VINTF `IVehicle/default` e `overrides` do `android.hardware.automotive.vehicle@V4-default-service`.
**Where**: `android/vhal/Android.bp` (+ `.rc` e `.xml` da placa ao lado)
**Depends on**: T10
**Reuses**: `vipee-vehicle-service` (mesmos `srcs` por `cc_defaults`)
**Requirement**: RPI-03

**Tools**:

- MCP: `context7` (Soong `overrides`, `vintf_fragments`)
- Skill: NONE

**Done when**:

- [ ] Build do alvo na árvore `aosp-rpi` passa (depois do T15; até lá, `m nothing` na árvore do emulador valida o `.bp`) — pendente: VM
- [x] Alvo do emulador `vipee-vehicle-service` inalterado (propriedades movidas sem mudança para o `cc_defaults`; `vhal-sync.sh` passa a copiar o `.xml` do fragmento)

**Status**: [x] checado na VM em 2026-11-05: `vm_m.sh selinux_policy vipee-vehicle-service vipee-vehicle-service-rpi` em `~/aosp` OK; em `~/aosp-rpi`, `m nothing` + `m vipee-vehicle-service-rpi vipee-evs-v4l2 vipee_bridge selinux_policy` OK para `aosp_rpi4_car` e `aosp_rpi5_car` (binário e `.rc` em `vendor/`; antes: `bpfmt -d` sem diferença; `host_init_verifier` aceita o `.rc`)

**Tests**: none
**Gate**: build

---

### T12: EVS na placa

**What**: `.rc` da placa que carrega o `v4l2loopback` do vendor com 4 devices em `40..43` e sobe o fork com o nome de serviço da HAL stock; `overrides` da `android.hardware.automotive.evs-v4l2`.
**Where**: `android/rpi/init.vipee_evs_rpi.rc` (+ entrada no `android/rpi/Android.bp`)
**Depends on**: T11
**Reuses**: `android/evs/init.vipee_evs.rc`, `android.hardware.automotive.evs-v4l2.rc` da placa
**Requirement**: RPI-10, RPI-12

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `.rc` válido (`host_init_verifier` do AOSP local aceita o arquivo; o build do T17 roda de novo com as passwd da placa)
- [ ] Sem `.ko`, o serviço da ponte e o VHAL sobem (RPI-12) — pendente: imagem (T17) e placa

**Status**: [~] build OK na VM em 2026-11-05: `vm_m.sh selinux_policy vipee-vehicle-service vipee-vehicle-service-rpi` em `~/aosp` OK; em `~/aosp-rpi`, `m nothing` + `m vipee-vehicle-service-rpi vipee-evs-v4l2 vipee_bridge selinux_policy` OK para `aosp_rpi4_car` e `aosp_rpi5_car`; falta a placa (`bpfmt -d` sem diferença em `android/rpi`, `android/evs`, `android/evs_hal`)

**Tests**: none
**Gate**: build

---

### T13: Gerador do `ros_prebuilts.bp`

**What**: Script que lê `push_arm64/lib` e o binário e escreve um `cc_prebuilt_library_shared` por lib e o `cc_prebuilt_binary` `vipee_bridge` (`init_rc`), falhando com erro nomeado se alguma `NEEDED` não estiver na lista nem na lista de libs do sistema/NDK.
**Where**: `tools/rpi/gen_prebuilts.py` (+ `tools/rpi/test_gen_prebuilts.py`)
**Depends on**: T12
**Reuses**: saída do T3 (`push_arm64`); `llvm-readelf -d` chamado pela stdlib (`subprocess`)
**Requirement**: RPI-04

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Testes RED antes do código: lista simples, `NEEDED` faltando, binário com `init_rc`
- [x] Gate check passes: `python3 -m unittest tools/rpi/test_gen_prebuilts.py && ruff check tools/rpi`
- [x] Correção pós-VM (2026-11-05): o `check_elf_file` do Soong recusou o binário (`DT_NEEDED "liblog.so" is not specified in shared_libs`); as libs do sistema que o binário pede, menos libc/libm/libdl (implícitas), entram em `shared_libs` (teste RED antes, 10/10)

**Tests**: unit
**Gate**: quick

---

### T14: Produto `vipee_rpi.mk` e `BoardConfigVipee.mk`

**What**: `PRODUCT_PACKAGES` e `PRODUCT_COPY_FILES` da ponte; `BOARD_VENDOR_SEPOLICY_DIRS` e remoção de `androidboot.selinux=permissive`; `prebuilt_etc` dos mapas, XML Cyclone da placa e `.rc` da ponte.
**Where**: `android/rpi/` (`vipee_rpi.mk`, `BoardConfigVipee.mk`, `Android.bp`; um conjunto coeso)
**Depends on**: T13
**Reuses**: lista de arquivos do `install-vendor.sh`
**Requirement**: RPI-01, RPI-02, RPI-03

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Toda peça que o `install-vendor.sh` empurra no emulador tem módulo correspondente (lista no cabeçalho do `vipee_rpi.mk`; libs ROS por `required` do `vipee_bridge`, com prefixo `vipee_ros_` no nome do módulo porque `libyaml` colide com o módulo do AOSP)
- [x] Revisão cruzada com o `check-image.sh` do T18 (verde na imagem rpi4 de 2026-11-06: permissive removido do `cmdline.txt`, `.ko` no vendor)

**Status**: [x] concluída (imagem rpi4 de 2026-11-06 e T18 verdes; antes: `unittest` 9/9, `ruff` OK, `bpfmt -d` sem diferença; `BoardConfigVipee.mk` avaliado com `make` num BoardConfig de teste: permissive removido, `.ko` só quando existe)

**Tests**: none
**Gate**: build

---

### T15: `rpi-sync.sh`

**What**: Monta `vendor/vipee/bridge` na árvore `aosp-rpi` da VM (copia `android/vhal`, `android/evs_hal`, `android/evs`, `android/evs_probe`, `android/rpi`, sepolicy, mapas, configs, `push_arm64`, `.ko`; gera o `.bp`) e acrescenta, de forma idempotente, o `inherit-product` aos `aosp_rpi{4,5}_car.mk` e o `-include` aos `BoardConfig.mk`.
**Where**: `tools/rpi/rpi-sync.sh`
**Depends on**: T14
**Reuses**: `tools/emu/vhal-sync.sh`, `evs-sync.sh`
**Requirement**: RPI-01

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] Rodar duas vezes deixa a árvore igual (sem linha duplicada): `RPI_HOST= RPI_TREE=<scratch>` com cópias mínimas dos `.mk` das placas, `sha256sum` de todos os arquivos igual entre as rodadas; linha de outro após a nossa faz a nossa voltar ao fim
- [x] `lunch aosp_rpi4_car-bp4a-userdebug && m nothing` passa na VM, com aviso prévio

**Status**: [x] checado na VM em 2026-11-05: `vm_m.sh selinux_policy vipee-vehicle-service vipee-vehicle-service-rpi` em `~/aosp` OK; em `~/aosp-rpi`, `m nothing` + `m vipee-vehicle-service-rpi vipee-evs-v4l2 vipee_bridge selinux_policy` OK para `aosp_rpi4_car` e `aosp_rpi5_car` (anexos idempotentes nos `.mk` das duas placas; antes: `bash -n` e `shellcheck` sem aviso além de SC2012 info; ponte arm64 recompilada em 2026-11-05 com a checagem de `NEEDED` verde)

**Tests**: none
**Gate**: build

---

### T16: `v4l2loopback.ko` para RPi4 e RPi5

**What**: Script que monta a árvore de kernel (`common-android16-6.12` + manifesto do raspberry-vanilla) na VM e compila o `v4l2loopback` para as duas placas; confere o `vermagic` contra um módulo do kernel pré-compilado; se não casar, compila o kernel inteiro e o deixa pronto para substituir `device/brcm/rpiN-kernel`.
**Where**: `tools/rpi/build-kernel-module.sh`
**Depends on**: None
**Reuses**: README do `android_kernel_manifest`
**Requirement**: RPI-10, RPI-14

**Tools**:

- MCP: `context7` (kleaf/DDK)
- Skill: NONE

**Done when**:

- [x] `modinfo` do `.ko` de cada placa com `vermagic` igual ao do kernel que vai na imagem (2026-11-05: `6.12.93-gb07e73ed17bf-v8 SMP preempt mod_unload modversions aarch64` nas duas; CRCs de modversions iguais em 56 de 82 símbolos, os 56 que algum módulo pré-compilado também importa, 0 diferentes; kernel pré-compilado reaproveitado, sem recompilar o kernel inteiro)
- [x] Builds de kernel avisados aos outros projetos (um `m`/bazel por vez)

**Status**: [x] concluída. Desvios: `ddk_module` com `deps` em `//common:all_headers_aarch64` (o `kernel_build` da placa não declara `ddk_module_headers`); `repo` no PATH para o `--config=stamp`; `.ko` sem assinatura (kernel com `MODULE_SIG` sem `MODULE_SIG_FORCE`; carga confirmada só na placa, T18/handoff)

**Tests**: integration
**Gate**: full

---

### T17: `build-image.sh` e imagem RPi4

**What**: Script na VM (`tmux` "rpi"): `lunch`, `m bootimage systemimage vendorimage`, `rpiN-mkimg.sh`, `xz`; gera a imagem da RPi4.
**Where**: `tools/rpi/build-image.sh`
**Depends on**: None
**Reuses**: README do raspberry-vanilla
**Requirement**: RPI-01

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `RaspberryVanillaAOSP16-<data>-rpi4_car.img.xz` existe na VM e no WSL (2026-11-06: `~/rpi_images/RaspberryVanillaAOSP16-20261106-rpi4_car.img.xz`, 662 MB)
- [x] Aviso a AVM e CalibDroid antes do `m`

**Status**: [x] concluída. Desvio: `m -j16` (variável `JOBS`); a primeira tentativa, com 32 jobs, morreu por OOM a 79% (VM de 61 GB sem swap, fase de dex).

**Tests**: none
**Gate**: build

---

### T18: `check-image.sh`

**What**: Checagem estática da imagem: partições extraídas, arquivos da ponte no vendor com rótulos SELinux, `NEEDED` sem `.so.N` externo, `cmdline.txt` sem `permissive`, `.ko` presente, VHAL e EVS stock ausentes.
**Where**: `tools/rpi/check-image.sh`
**Depends on**: T17
**Reuses**: lista do T14
**Requirement**: RPI-01, RPI-02, RPI-03, RPI-04

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [x] `tools/rpi/check-image.sh rpi4` verde na imagem do T17 (2026-11-06, no `.img.xz` e em `--parts`; vendor com 118 MB livres dos 384 MB, 197 libs em `/vendor/lib64`, `v4l2loopback.ko` presente; `cmdline.txt` = `console=ttyS0,115200 androidboot.hardware=rpi4 androidboot.verifiedbootstate=orange`)
- [x] Falha comprovada: com um arquivo da ponte removido de uma cópia, o script reprova (RED): cópia do vendor sem `/bin/vipee_bridge` ➔ `FAIL MISSING /vendor/bin/vipee_bridge`, rc=1; cópia do boot com `androidboot.selinux=permissive` no `cmdline.txt` ➔ `FAIL PERMISSIVE`, rc=1; original de novo ➔ OK

**Status**: [x] concluída. Desvio: sem `mtools` no WSL, o `cmdline.txt` é lido dos bytes do FAT (linha com `androidboot.hardware=`); o rótulo SELinux sai do formato `security.selinux (N) = "..."` do `debugfs ea_get`.

**Tests**: integration
**Gate**: full

---

### T19: Imagem RPi5

**What**: `build-image.sh rpi5` e `check-image.sh rpi5`.
**Where**: `tools/rpi/build-image.sh` (execução; sem mudança se o script já cobre a placa)
**Depends on**: T18
**Reuses**: T17, T18
**Requirement**: RPI-13, RPI-14

**Tools**:

- MCP: NONE
- Skill: NONE

**Done when**:

- [ ] Imagem da RPi5 gerada e `check-image.sh rpi5` verde
- [ ] Se falhar, falha registrada no `STATE.md` e a RPi4 entregue mesmo assim

**Status**: [ ] adiada por decisão do Wendell (2026-11-06): as placas RPi5 ainda estão em compra e cada build completo custa 2–3 h de VM, agora paga (créditos esgotados). Prontos para quando houver placa: `v4l2loopback.ko` do rpi5 (T16), camada vendor sincronizada e módulos checados em `aosp_rpi5_car` (T10, T11, T15). Rodar `tools/rpi/build-image.sh rpi5` e `tools/rpi/check-image.sh rpi5 <img>`.

**Tests**: integration
**Gate**: full

---

### T20: Handoff de instalação e execução

**What**: `Refs/handoff-rpi-image.md` em pt-BR: baixar a imagem, gravar o SD, ligar (HDMI, USB, rede), definir o host, expor a porta 7400 do WSL na LAN, rodar `fake_sim` e bag, conferir cada passo (comando e saída esperada), recuo para permissive, problemas conhecidos.
**Where**: `Refs/handoff-rpi-image.md`
**Depends on**: T19
**Reuses**: `README.md`, `tools/emu/test-e2e.sh`, `Refs/handoff-ros2-jazzy-aaos.md` (achados do RPi4)
**Requirement**: RPI-15, RPI-16, RPI-17

**Tools**:

- MCP: `context7` (WSL rede espelhada, se preciso)
- Skill: NONE

**Done when**:

- [x] Cada passo de conferência tem comando e saída esperada (seções 2, 4, 5, 6 e 7; itens não confirmados sem placa marcados como tal)
- [x] README aponta para o handoff (Etapas de validação, item 2)

**Status**: [x] concluída sem a T19 (adiada): o handoff cobre a RPi4 e diz como gerar a RPi5. Usa o bag de referência de 2026-11-05 (`--rate 3` para conferir taxa) e lista o travamento DDS de 2026-11-05 e o vazamento de buffers do EVS em Problemas conhecidos.

**Tests**: none
**Gate**: build

---

## Phase Execution Map

```
Phase 1 → Phase 2 → Phase 3 → Phase 4 → Phase 5 → Phase 6

Phase 1:  T1 → T2 → T3
Phase 2:  T4 → T5 → T6
Phase 3:  T7 → T8 → T9
Phase 4:  T10 → T11 → T12 → T13 → T14 → T15
Phase 5:  T16
Phase 6:  T17 → T18 → T19 → T20
```

---

## Task Granularity Check

| Task | Scope | Status |
| ---- | ----- | ------ |
| T1 | 1 arquivo de toolchain | ✅ Granular |
| T2, T3 | 1 script cada | ✅ Granular |
| T4 | 1 função pura + teste | ✅ Granular |
| T5 | 1 função (`main`) | ✅ Granular |
| T6 | 1 passo de teste + XML que ele usa | ⚠️ OK, coeso |
| T7 | 2 mapas, mesma linha | ⚠️ OK, coeso |
| T8 | config EVS em `android/evs/` | ⚠️ OK, coeso (uma mudança de número) |
| T9 | testes e instalação nos números novos | ⚠️ OK, coeso |
| T10 | política da ponte | ⚠️ OK, coeso |
| T11 | 1 alvo Soong | ✅ Granular |
| T12 | 1 `.rc` | ✅ Granular |
| T13 | 1 script + teste | ✅ Granular |
| T14 | produto da ponte em `android/rpi/` | ⚠️ OK, coeso |
| T15 | 1 script | ✅ Granular |
| T16 | 1 script | ✅ Granular |
| T17, T18 | 1 script cada | ✅ Granular |
| T19 | execução | ✅ Granular |
| T20 | 1 documento | ✅ Granular |

## Diagram-Definition Cross-Check

| Task | Depends On (task body) | Diagram Shows | Status |
| ---- | ---------------------- | ------------- | ------ |
| T1 | None | início da fase 1 | ✅ Match |
| T2 | T1 | T1 → T2 | ✅ Match |
| T3 | T2 | T2 → T3 | ✅ Match |
| T4 | None | início da fase 2 | ✅ Match |
| T5 | T4 | T4 → T5 | ✅ Match |
| T6 | T5 | T5 → T6 | ✅ Match |
| T7 | None | início da fase 3 | ✅ Match |
| T8 | T7 | T7 → T8 | ✅ Match |
| T9 | T8 | T8 → T9 | ✅ Match |
| T10 | None | início da fase 4 | ✅ Match |
| T11 | T10 | T10 → T11 | ✅ Match |
| T12 | T11 | T11 → T12 | ✅ Match |
| T13 | T12 | T12 → T13 | ✅ Match |
| T14 | T13 | T13 → T14 | ✅ Match |
| T15 | T14 | T14 → T15 | ✅ Match |
| T16 | None | fase 5 | ✅ Match |
| T17 | None (fases anteriores) | início da fase 6 | ✅ Match |
| T18 | T17 | T17 → T18 | ✅ Match |
| T19 | T18 | T18 → T19 | ✅ Match |
| T20 | T19 | T19 → T20 | ✅ Match |

## Test Co-location Validation

| Task | Code Layer Created/Modified | Matrix Requires | Task Says | Status |
| ---- | --------------------------- | --------------- | --------- | ------ |
| T1-T3 | Build/config | none | none | ✅ OK |
| T4 | Núcleo | unit | unit | ✅ OK |
| T5 | `main.cpp` | integration | integration | ✅ OK |
| T6 | teste e2e + XML | integration | integration | ✅ OK |
| T7 | Mapas (config) | none | none | ✅ OK |
| T8 | Config EVS | none | none | ✅ OK |
| T9 | Câmeras renumeradas | integration | integration | ✅ OK |
| T10-T12 | sepolicy, Soong, `.rc` | none | none | ✅ OK |
| T13 | Gerador Python | unit | unit | ✅ OK |
| T14, T15 | `.mk`, `.bp`, script de sync | none | none | ✅ OK |
| T16 | `.ko` | integration | integration | ✅ OK |
| T17 | script de build | none | none | ✅ OK |
| T18, T19 | Imagem | integration | integration | ✅ OK |
| T20 | doc | none | none | ✅ OK |
