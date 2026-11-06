# rpi-image: validação

**Data**: 2026-11-06
**Spec**: `.specs/features/rpi-image/spec.md`
**Diff range**: `dd356ae^..c3c938e` (T1, toolchain arm64 com `max-page-size=16384`, até o handoff)
**Verifier**: sub-agente independente (autor ≠ verificador), contexto novo
**Artefatos inspecionados (só leitura)**: `~/rpi_images/RaspberryVanillaAOSP16-20261106-rpi4_car.img.xz` e as partições extraídas `boot.img` (FAT) e `vendor.img` (ext4), mais a partição 5 (system/product) extraída para conferir a RRO; `~/ros2_android_ws/rpi_modules/{rpi4,rpi5}/v4l2loopback.ko`; `~/ros2_android_ws/bridge_build_arm64/vipee_bridge`.
**Limites respeitados**: VM `vipee` parada, sem `aws`, sem `adb`, emulador intocado, nada escrito em `~/aosp`. Placa física indisponível.

---

## Validation: rpi-image — PASS COM PENDÊNCIAS

Tudo o que dá para conferir sem placa passou: imagem rpi4 gerada e íntegra, suítes verdes, 12/12 mutantes de código de produção mortos. Restam 9 critérios que só a RPi4 confirma (PENDENTE-PLACA), 2 adiados com a T19 (RPi5) e 5 lacunas de severidade Major ou Minor, listadas em "Lacunas". A G1 deve ser corrigida antes de alguém seguir o handoff na placa.

| Situação | Qtde | Critérios |
| --- | --- | --- |
| PASS | 6 | RPI-01, RPI-04, RPI-05, RPI-15, RPI-16, RPI-17 |
| PENDENTE-PLACA | 9 | RPI-02, RPI-03, RPI-06, RPI-07, RPI-08, RPI-09, RPI-10, RPI-11, RPI-12 |
| ADIADO (T19) | 2 | RPI-13, RPI-14 |
| Reprovado | 0 | - |

---

## Tasks

| Task | Situação | Nota |
| --- | --- | --- |
| T1–T9 | Concluídas | T6 e T9 verdes no emulador, pelo registro do autor (o emulador não foi tocado nesta verificação) |
| T10, T11, T13–T18, T20 | Concluídas | T10/T11/T15 checadas na VM em 2026-11-05 (registro do autor) |
| T12 | Parcial | `.rc` válido e presente na imagem; "sem `.ko` a ponte e o VHAL sobem" depende da placa |
| T19 | Adiada | Decisão do Wendell (2026-11-06): RPi5 em compra |

---

## Critérios de aceite, ancorados no spec

| Critério | Resultado definido no spec | Evidência (`file:line` / comando) | Resultado |
| --- | --- | --- | --- |
| RPI-01 imagem gravável `aosp_rpi4_car` | arquivo de imagem do `rpi4-mkimg.sh` | `tools/rpi/build-image.sh:1`; `~/rpi_images/RaspberryVanillaAOSP16-20261106-rpi4_car.img.xz` (662 MB); `sfdisk -d` mostra as partições 1 (FAT), 5, 6 e 7; `tools/rpi/check-image.sh rpi4 --parts <img>` ➔ `check-image rpi4: OK`, rc=0 | PASS |
| RPI-02 boot em `Enforcing` | `sys.boot_completed=1` e `Enforcing` | Estático: `android/rpi/BoardConfigVipee.mk:11` (filter-out do permissive); `cmdline.txt` da imagem = `console=ttyS0,115200 androidboot.hardware=rpi4 androidboot.verifiedbootstate=orange`, 0 ocorrências de `selinux=permissive` no `boot.img`; checagem em `tools/rpi/check-image.sh:82`. O boot só a placa confirma | PENDENTE-PLACA |
| RPI-03 ponte em `vendor_vipee_ros2` e VHAL da ponte no lugar do stock | processo no domínio, VHAL stock ausente | Estático: rótulos no `vendor.img` (`vipee_bridge` = `vendor_vipee_ros2_exec`, `vipee-vehicle-service-rpi` = `hal_vehicle_default_exec`, `vipee-evs-v4l2` = `hal_evs_default_exec`); `/vendor/bin/hw` sem `android.hardware.automotive.vehicle@V4-default-service` nem `evs-v4l2` stock (`tools/rpi/check-image.sh:60`); `overrides` em `android/vhal/Android.bp:54` e `android/evs_hal/Android.bp:84`; VINTF `IVehicle/default` v4 só em `/vendor/etc/vintf/manifest/vipee-vehicle-service-rpi.xml`; `.rc` com o nome `vendor.vehicle-hal-default` | PENDENTE-PLACA |
| RPI-04 libs em `/vendor`, sem `.so.N` externo | nenhuma NEEDED versionada | `tools/rpi/test_gen_prebuilts.py:76` - `assertEqual(str(cm.exception), "NEEDED_UNRESOLVED libb.so libfoo.so.1")`; `tools/rpi/test_gen_prebuilts.py:55` (NEEDED faltando); `tools/rpi/check-image.sh:72`. Na imagem: 197 `.so` em `/vendor/lib64`, 0 NEEDED com `.so.N` (`llvm-readelf -d` em todos); as NEEDED sem lib no vendor são só do sistema (`libc`, `libm`, `libdl`, `liblog`, `libEGL`, `libbinder_ndk`...). Página de 16 KB: 776/776 segmentos `LOAD` com `Align 0x4000` (`tools/jazzy/android_arm64.toolchain.cmake:17`). O `vipee_bridge` da imagem tem o mesmo Build ID (`9a8a66e3…`) do `bridge_build_arm64`, só que sem símbolos | PASS |
| RPI-05 host vazio ➔ `DDS_PEER_UNSET`, propriedades `UNAVAILABLE` | log `DDS_PEER_UNSET`; propriedades `UNAVAILABLE` | `core/test/dds_peer_test.cpp:32` - `EXPECT_EQ(d.error, DdsPeerError::kUnset)` e `EXPECT_STREQ(DdsPeerErrorName(d.error), "DDS_PEER_UNSET")`; inválido: `core/test/dds_peer_test.cpp:44` (`kInvalid`, `"DDS_PEER_INVALID"`); `ros/vipee_bridge/src/main.cpp` (`applyDdsPeer`: loga o código e fica em `pause()`); e2e no emulador `tools/emu/test-e2e.sh:157` (passo `peer`: log, PID estável, `running`). A metade `UNAVAILABLE` só tem evidência no código (`android/vhal/VipeeVehicleHardware.cpp:85`), sem asserção no passo `peer` (lacuna G3) | PASS (com G3) |
| RPI-06 host definido ➔ conexão TCP com o host na 7400 | conexão estabelecida | `tools/jazzy/config/cyclonedds_rpi.xml:8` (`${VIPEE_HOST}:7400`), XML idêntico no `vendor.img`; e2e no emulador `tools/emu/test-e2e.sh:179` (passo 5b com `10.0.2.2`). Na LAN, só a placa confirma. Precisão do spec: "estabelecer a conexão" não tem saída observável definida (G5) | PENDENTE-PLACA |
| RPI-07 velocidade, marcha e freio no `CarService` | valores do mapa | Mesmos caminhos do emulador (`ct-speed-slice`); passos e saídas esperadas em `Refs/handoff-rpi-image.md:123` | PENDENTE-PLACA |
| RPI-08 dado velho ➔ `UNAVAILABLE` | limite do mapa | `Refs/handoff-rpi-image.md:131` | PENDENTE-PLACA |
| RPI-09 latência registrada | número no `validation.md` | Ainda sem medida. O roteiro de medição (`Refs/handoff-rpi-image.md:133`) não é seguro contra a placa (G1) | PENDENTE-PLACA |
| RPI-10 `/dev/video40..43` criados pelo `v4l2loopback` da imagem | devices existem | Estático: `/vendor/lib/modules/v4l2loopback.ko` + `modules.dep`; `android/rpi/init.vipee_evs_rpi.rc:16` (`video_nr=40,41,42,43`); `vermagic` do `.ko` = `6.12.93-gb07e73ed17bf-v8 SMP preempt mod_unload modversions aarch64`, igual ao `Linux version 6.12.93-gb07e73ed17bf-v8` do kernel no `boot.img` da imagem e igual ao `.ko` de `rpi_modules/rpi4` (`cmp`); config EVS do vendor só com `video40..43`; mapas em `maps/cat793f.yaml:25` e `maps/commander.yaml:23`; RRO `VipeeEvsCameraOverlay.apk` em `/product/overlay`. A carga real (assinatura, CRCs) só a placa confirma | PENDENTE-PLACA |
| RPI-11 4 câmeras 1280×720 a ≥ 8 quadros/s | ≥ 8 fps cada | `vipee_evs_probe` presente no vendor; roteiro em `Refs/handoff-rpi-image.md:152` | PENDENTE-PLACA |
| RPI-12 sem módulo ➔ `DEVICE_UNAVAILABLE`, telemetria segue | log por câmera | `core/src/camera_channel.cpp:101` (código já existente); `android/rpi/BoardConfigVipee.mk:16` (`wildcard`: imagem sai sem o `.ko` se faltar); falta a placa | PENDENTE-PLACA |
| RPI-13 imagem RPi5 | imagem do `rpi5-mkimg.sh` | T19 adiada | ADIADO |
| RPI-14 `.ko` da RPi5 embutido | `.ko` no vendor da RPi5 | `.ko` do rpi5 existe, `vermagic` igual ao do rpi4 (mesmo kernel `-v8`); embutir depende da T19 | ADIADO |
| RPI-15 handoff cobre os tópicos | download, gravação, ligação, host, porta 7400, `fake_sim` e bag, conferência | `Refs/handoff-rpi-image.md:14` (download e gravação), `:33` (ligação), `:81` (porta 7400 com `portproxy`), `:99` (host e `fake_sim`), `:135` (bag), `:61` (conferência) | PASS |
| RPI-16 comando e saída esperada por passo | todo passo com as duas coisas | `Refs/handoff-rpi-image.md:63-78`, `:118-128`, `:149-153`, `:164`. Todos os caminhos citados existem na imagem: `/vendor/bin/vipee_evs_probe`, `/vendor/bin/modprobe`, `/vendor/etc/vipee/maps/{cat793f,commander}.yaml`, `/vendor/etc/vipee/cyclonedds_device.xml`, `.rc` da ponte. Ressalva: o passo de latência (`:133`) está errado (G1) | PASS (com G1) |
| RPI-17 problemas conhecidos com sintoma e correção | tabela | `Refs/handoff-rpi-image.md:168-185` (12 linhas). Ressalva: a linha `:175` diz que nome que não resolve dá `DDS_PEER_INVALID`, mas `core/src/dds_peer.cpp` não resolve DNS (G4) | PASS (com G4) |

### Casos de borda do spec

- [ ] Host inalcançável ➔ `UNAVAILABLE` sem reinício em laço: PENDENTE-PLACA, sem teste no emulador.
- [x] Build de uma placa falha ➔ entrega a outra: a RPi4 saiu, a RPi5 está registrada como adiada (`tasks.md`, T19).
- [x] Regravar o SD: a configuração vem toda da imagem; só `persist.vendor.vipee.host` precisa ser redefinida, e o handoff manda fazer isso (`Refs/handoff-rpi-image.md:119`).

---

## Sensor de discriminação

Worktree descartável (`git worktree add --detach`) no scratchpad, removida no fim. As imagens mutadas foram cópias do `boot.img` e do `vendor.img` (`debugfs -w` só nas cópias). `git status --porcelain` da árvore real antes e depois: ` M .specs/STATE.md` nas duas vezes.

| # | Arquivo | Falha injetada | Morto? |
| --- | --- | --- | --- |
| M1 | `tools/rpi/gen_prebuilts.py:84` | Sem as libs de sistema (`liblog`) no `shared_libs` | Sim (2 falhas) |
| M2 | `tools/rpi/gen_prebuilts.py:71` | `NEEDED_UNRESOLVED` não é mais levantado | Sim (3) |
| M3 | `tools/rpi/gen_prebuilts.py:84` | libc/libm/libdl implícitas entram no `shared_libs` | Sim (2) |
| M9 | `tools/rpi/gen_prebuilts.py:76` | NEEDED das libs não checadas | Sim (2) |
| M10 | `tools/rpi/gen_prebuilts.py:87` | `required` só com as NEEDED do binário (sem as de `dlopen`) | Sim (1) |
| M11 | `tools/rpi/gen_prebuilts.py:78` | Sem `stem` (arquivo instalado com o prefixo) | Sim (1) |
| M12 | `tools/rpi/gen_prebuilts.py:70` | `.so.N` externo aceito | Sim (1) |
| M4 | `core/src/dds_peer.cpp:23` | Host vazio inicia o DDS | Sim (`DdsPeer.EmptyValueIsUnset`) |
| M5 | `core/src/dds_peer.cpp:11` | `:` aceito no host | Sim (`InvalidCharactersAreRejected`) |
| M6 | `core/src/dds_peer.cpp:27` | Sem checagem de caractere | Sim |
| M7 | `core/src/dds_peer.cpp:18` | XML sem variável com host definido exporta `VIPEE_HOST` | Sim (`XmlWithoutVariable…`) |
| M8 | `core/src/dds_peer.cpp:40` | Nomes de erro trocados | Sim |
| C1 | `tools/rpi/check-image.sh:82` | Checagem de permissive enfraquecida | **Não**: nenhum teste guarda o script |
| C2 | `tools/rpi/check-image.sh:55` | `MISSING` removido | **Não** |
| C3 | `tools/rpi/check-image.sh:72` | Só `.so.NN` (dois dígitos) reprova | **Não** |

O `check-image.sh` original discrimina as entradas ruins. Cópia do boot com `androidboot.selinux=permissive` ➔ `FAIL PERMISSIVE`, rc=1. Cópia do vendor sem `/bin/vipee_bridge` ➔ `FAIL MISSING /vendor/bin/vipee_bridge`, rc=1. `vipee_bridge` com NEEDED `librcl_yaml_param_pars.so.1` ➔ `FAIL NEEDED_VERSIONED`, rc=1. Placa errada (`rpi5` na imagem rpi4) ➔ `FAIL BOARD`, rc=1. Já o próprio gate não tem teste automático: os C1–C3 sobrevivem, e o RED da T18 só foi feito à mão (G2).

O `main.cpp` (`applyDdsPeer`) não passou pelo sensor: o único teste dele é o passo `peer` no emulador, e o emulador está fora dos limites.

**Profundidade**: leve ampliada (15 mutações). **Placar**: 12/15 mortos; os 3 sobreviventes são do gate estático (G2).

---

## Gate

- `cmake --preset default && cmake --build --preset default && ctest --preset default`: **138/138 verdes** (eram 134 antes da feature; +4 de `dds_peer_test`).
- `python3 -m unittest tools/rpi/test_gen_prebuilts.py`: **10/10 OK** (novos).
- `uvx ruff check tools/rpi`: `All checks passed!`
- `tools/rpi/check-image.sh rpi4 --parts <boot.img, vendor.img>`: `check-image rpi4: OK`.
- Testes pulados: nenhum. Nenhum teste apagado nem enfraquecido no diff.
- O `test-e2e.sh peer` e o `test-cam.sh` (emulador) não rodaram de novo aqui: o emulador está fora dos limites. Vale o registro verde do autor na T6 e na T9.

---

## Corretude de API (context7)

- **CycloneDDS** (`/websites/deepwiki_ros2_rmw_cyclonedds`): a configuração aceita expansão `${VAR}` de variável de ambiente (exemplo oficial `${CYCLONEDDS_PID}`), então `<Peer Address="${VIPEE_HOST}:7400"/>` é válido, desde que `VIPEE_HOST` esteja no ambiente antes do `rclcpp::init`, que é o que o `main.cpp` faz. O mesmo documento mostra que `CYCLONEDDS_URI` também aceita **XML inline**, não só `file://`; o `applyDdsPeer` não trata esse caso (G4). Sobre `TCP/Port = -1`, o context7 não tem a página; pela referência de configuração do Cyclone, `-1` desliga o socket de escuta (o nó só disca), o que bate com "sem porta de escuta" (`tools/jazzy/config/cyclonedds_rpi.xml:2`) e com o passo 5b verde no emulador.
- **Kleaf `ddk_module`**: o context7 não tem a documentação do Kleaf (só a de GKI e symbol lists). Fica indireto: `ddk_module(out=, kernel_build=, deps=)` compilou na VM e o `vermagic` do `.ko` é idêntico à string `Linux version` do kernel no `boot.img` da imagem. Os CRCs de modversions (56/82 conferidos pelo autor) e a carga com `MODULE_SIG` sem `FORCE` só a placa confirma.

---

## Handoff (`Refs/handoff-rpi-image.md`) contra RPI-15..17 e a imagem

- Cobertura (RPI-15), comando + saída por passo (RPI-16) e problemas conhecidos (RPI-17): conferem.
- Caminhos citados e presentes na imagem: `/vendor/bin/vipee_bridge`, `/vendor/bin/hw/vipee-vehicle-service-rpi`, `/vendor/bin/hw/vipee-evs-v4l2`, `/vendor/bin/vipee_evs_probe`, `/vendor/bin/modprobe`, `/vendor/etc/vipee/maps/*.yaml`, `/vendor/etc/vipee/cyclonedds_device.xml` com `${VIPEE_HOST}`, `vendor_property_contexts` com `persist.vendor.vipee.host`, RRO em `/product/overlay/VipeeEvsCameraOverlay.apk`.
- Defeito: o passo de latência (G1).
- Imprecisão: o `DDS_PEER_INVALID` atribuído a nome que não resolve (G4).

---

## Lacunas (ordenadas)

1. **G1 (Major), handoff de latência destrutivo.** RPI-09/RPI-16, `Refs/handoff-rpi-image.md:133`. O handoff manda rodar `tools/emu/test-e2e.sh` contra a placa. Só que o script inteiro termina no passo `peer` (`tools/emu/test-e2e.sh:206`), e esse passo (`:157`) faz `adb remount`, empurra `bridge_build_x86_64/vipee_bridge` (x86_64) para `/vendor/bin` da placa, põe `persist.vendor.vipee.host=10.0.2.2` e, na saída, limpa a propriedade. Antes disso o script ainda exige o Wi-Fi do emulador (`:189`) e sobe o próprio `fake_sim`. Resultado: a ponte da placa fica quebrada até regravar o SD. Correção: um modo só de latência, seguro para a placa, no `test-e2e.sh`, ou um procedimento manual de medida no handoff.
2. **G2 (Minor), gate estático sem teste.** RPI-01..04, `tools/rpi/check-image.sh:55,72,82`. Os mutantes C1–C3 sobrevivem. Correção: um teste de shell pequeno que gera cópias ruins do `vendor.img`/`boot.img` (como foi feito aqui) e confere `FAIL MISSING`/`FAIL PERMISSIVE`/`FAIL NEEDED_VERSIONED` com rc=1.
3. **G3 (Minor), UNAVAILABLE sem asserção.** RPI-05, `tools/emu/test-e2e.sh:170-177`. O passo `peer (a)` confere o log e o PID, mas não as propriedades `UNAVAILABLE`, que o spec pede. Correção: `get $SPEED | grep "$NA"` no passo (a).
4. **G4 (Minor), host inválido mal detectado.** Fail-loud, `ros/vipee_bridge/src/main.cpp` (`applyDdsPeer`). Com `CYCLONEDDS_URI` inline (XML) ou com arquivo ilegível, a função conclui "sem variável" e o DDS sobe sem `VIPEE_HOST`, sem erro nomeado. Não acontece hoje (o `.rc` usa `file://`). Também, nome que não resolve passa no `DecideDdsPeer`; o handoff (`Refs/handoff-rpi-image.md:175`) diz o contrário.
5. **G5 (spec-precision), critérios sem saída observável e traceability parada.** RPI-06 não define o que é "conexão estabelecida" (log? propriedade AVAILABLE?), e RPI-03 "registrado no lugar do VHAL padrão" não tem passo de conferência no handoff além do `ps` (falta, por exemplo, `dumpsys -l | grep IVehicle` ou `lshal`). A tabela de rastreabilidade do spec continua `Pending` e "0 mapped" (`.specs/features/rpi-image/spec.md:158-176`).

Risco residual (placa): o `vendor/lib/modules/modules.load` traz `v4l2loopback.ko`. Se algo na placa carregar a lista antes do `on init` (nada no vendor faz isso hoje), o módulo sobe com os parâmetros padrão, e o `modprobe` do `init.vipee_evs_rpi.rc:16` não cria `video40..43`. Conferir no primeiro boot com `ls /dev/video4*`.

---

## Atualização da rastreabilidade (sugerida)

| Requisito | Novo estado |
| --- | --- |
| RPI-01, RPI-04, RPI-05, RPI-15, RPI-16, RPI-17 | Verificado |
| RPI-02, 03, 06–12 | Verificado estático; pendente na placa |
| RPI-13, RPI-14 | Adiado (T19) |

## Resumo

**Geral**: PASS COM PENDÊNCIAS. Na checagem ancorada no spec, 6 critérios passam e 9 dependem da placa; há 1 spec-precision gap (G5). Sensor: 12/15 mutantes mortos (os 3 sobreviventes estão no gate estático). Gate: 138 C++ + 10 Python verdes.
**Próximos passos**: corrigir a G1 antes do teste na placa; G2–G4 como fix tasks; medir e registrar a latência (RPI-09) aqui quando a RPi4 rodar.

## Correções depois da verificação (orquestrador, 2026-11-06)

- **G1 corrigida** em `Refs/handoff-rpi-image.md`: a medição de latência (RPI-09) não chama mais o `tools/emu/test-e2e.sh`. Agora é um laço que troca a velocidade no `state.json` do `fake_sim` e espera, no device, o `CarService` mostrar o valor. O handoff avisa para não rodar o e2e contra a placa. Os blocos de saída esperada da seção 6 passaram ao formato real do `cmd car_service get-property-value` (`Value: 5.0 METER_PER_SEC`, `Value: GEAR_DRIVE(0x8)`, `Value: FALSE`), tirado dos regex do `test-e2e.sh`.
- **G4, parte do handoff**: a linha de `DDS_PEER_INVALID` em Problemas conhecidos agora diz o que o `dds_peer.cpp` rejeita: caractere fora de letras, dígitos, `.` e `-`.
- **Risco residual do `modules.load`**: nenhum `.rc` do vendor da imagem carrega essa lista, e `/vendor/bin/modprobe` existe. O ramdisk não foi conferido. O sintoma (`/dev/video0..3` no lugar de 40..43) e o contorno entraram em Problemas conhecidos.
- **Em aberto, fora desta feature**: G2 (sem teste automático do `check-image.sh`), G3 (o passo `peer` não confere `UNAVAILABLE`), G4 no código (`CYCLONEDDS_URI` com XML inline) e G5 (precisão da spec). Ficam como candidatas a correção; nenhuma impede o teste na placa.
