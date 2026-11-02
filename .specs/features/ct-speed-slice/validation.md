# ct-speed-slice Validation

## Validation: PASS (ciclo 2 de 3, re-verificação)

**Date**: 2026-11-02
**Spec**: `.specs/features/ct-speed-slice/spec.md` (CTS-19 emendado em `2667c74`)
**Diff range**: `a369c5b^..2667c74`; re-verificado só o delta `f72c91c..2667c74` (`e7515a7`, `1834de4`, `f72f2bb`, `2667c74`)
**Verifier**: sub-agente independente (autor ≠ verificador)

Os 5 gaps do ciclo 1 estão fechados. Sobrevivente remanescente: nenhum. Gate unit 81/81, ROS 2/2 (inclui o novo `unreadable_map_test`), `test-vhal.sh` e `test-e2e.sh` verdes no emulador.

### Gaps do ciclo 1 ➔ estado

| Gap | Correção | Evidência (`file:line` + asserção) | Estado |
| --- | -------- | ---------------------------------- | ------ |
| 1. M21/M18 sobreviviam no ingest do VHAL | dois testes novos | `android/vhal/test/vhal_ingest_test.cpp:110` `EXPECT_EQ(...action, kWrite)` nas 4 propriedades; `:119-120` `expire(400ms)` falso e `expire(551ms)` verdadeiro só com update | ✅ M18 e M21 mortos |
| 2. CTS-23 mapa ilegível saía com `fatal` | `ros/vipee_bridge/src/main.cpp:77-92` refaz o `rclcpp::init` sem `--params-file <path>`; o nó sobe sem mapa e o `Bridge` loga `MAP_INVALID` | `ros/vipee_bridge/CMakeLists.txt:39-40` `[ $rc -eq 124 ] && [ $(grep -c 'UNAVAILABLE MAP_INVALID' ...) -eq 4 ]` (processo vivo 3 s, 4 linhas); rodado aqui: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` 2/2 | ✅ CTS-23 coberto |
| 3. e2e só checava a velocidade | latência e dado velho nas 4 propriedades | `tools/emu/test-e2e.sh:80-89` (as 4 casam no mesmo laço do device; `worst -le 200`), `:98-105` (as 4 `Can not get`, `STALE` de cada uma no logcat, `-le 600`) | ✅ |
| 4. dizimação abaixo da taxa assinada | VHAL repassa toda amostra (`EventMode::ALWAYS`, `android/vhal/VipeeVehicleHardware.cpp:174`); dizimação e seus testes removidos | `android/vhal/test/vhal_ingest_test.cpp:132-136` toda amostra a 15 ms vira `kWrite` com o próprio carimbo (67 em 1 s); `tools/emu/test-vhal.sh:137` `n >= 5*h` | ✅ (decisão julgada abaixo) |
| 5. CTS-19 mudança só de motivo | spec emendado ("ou, com o mesmo status, de motivo") | `core/test/bridge_test.cpp:474-475` `UNKNOWN_GEAR` ➔ `STALE` com status `UNAVAILABLE` | ✅ M7 morto |

### Decisão do Gap 4 (sem dizimação no VHAL), julgada

- **Contrato AOSP**: com VUR desligado, o `IVehicleHardware` "must generate at least" a taxa assinada (`hardware/include/IVehicleHardware.h:173-174`). Repassar ~66 Hz cumpre; nada no contrato proíbe exceder o `maxSampleRate` 10 do config. O `vipee_properties.json` não declara `supportVariableUpdateRate`, então o VUR fica desligado e o `ALWAYS` é o modo correto. O `DefaultVehicleHal` entrega os eventos aos clientes assinados sem dizimar por taxa (`SubscriptionManager::getSubscribedClients`); quem dizima por app é o `ContCarPropertyEventTracker.java:127`. ✅
- **CTS-10**: sem dizimação, nenhum valor novo de velocidade espera um período para virar evento; a latência só melhora.
- **Custo e limite**: ~66 eventos/s de um escalar pelo binder até o `CarService`, desprezível no emulador; reavaliar no RPi5. A taxa vista pelo app continua um pouco abaixo da assinada (com chegada a 15 ms e período de 200 ms, o cliente aceita a cada 210 ms ≈ 4,8 Hz), mas isso é regra do cliente do AOSP, fora do VHAL. A medição `rate` conta escritas no VHAL (`mSpeedEvents`), não eventos no app; é o lugar certo para o contrato do `IVehicleHardware`.
- **Rastreabilidade**: design ("Taxa da contínua", duas linhas) e T15 registram a revisão. Aceito.

### Discrimination Sensor (ciclo 2)

Worktree descartável em `scratchpad/verify-wt` no `2667c74`; unit por `ctest --preset default -L unit`, ROS pelo `run.sh` da worktree (monta a worktree em `/bridge`). Worktree removida; `git status --porcelain` igual ao baseline. O diretório de build compartilhado `~/ros2_android_ws/ros_test/build_bridge` ficou com o binário mutante (mesmo caminho `/bridge`, mtime mais novo); foi apagado e refeito do repo real, que passou 2/2.

| Mutation | File:line | Description | Killed? |
| -------- | --------- | ----------- | ------- |
| M7 | `core/src/bridge.cpp:95` | log só em mudança de status | ✅ Killed (`ReasonOnlyChangeIsLogged`, por SEGFAULT: `.back()` em vetor vazio, ver nota) |
| M18 | `android/vhal/include/vipee/vhal_ingest.h` (update ➔ `last_rx_ns_`) | update válido não alimenta o watchdog | ✅ Killed (`UpdatesFeedTheWatchdog`) |
| M21 | `vhal_ingest.h` (lista de propriedades) | `CURRENT_GEAR` descartada | ✅ Killed (`AllFourServedPropertiesAreWritten`) |
| M20 | - | código removido junto com a dizimação | N/A |
| N1 | `vhal_ingest.h` (`res.action`) | VHAL volta a dizimar a velocidade (descarta metade das amostras) | ✅ Killed (`EverySpeedSampleIsWrittenWithItsStamp`) |
| N2 | `vhal_ingest.h` (piso) | carimbo trocado pelo instante de recepção no VHAL | ✅ Killed (`RecordsOlderThanMarkerAreRaisedToIt`) |
| M16 | `vhal_ingest.h` (watchdog) | `>` ➔ `>=` (regressão) | ✅ Killed |
| M17 | `vhal_ingest.h` (piso) | remove o piso (regressão) | ✅ Killed |
| R1 | `ros/vipee_bridge/src/main.cpp:87` | não pula o caminho depois de `--params-file` | ✅ Killed (`unreadable_map_test`) |
| R2 | `ros/vipee_bridge/src/main.cpp:91` | refaz o init com os argumentos originais | ✅ Killed (`unreadable_map_test`, `fatal`) |

**Sensor depth**: expandido. **Resultado do ciclo 2**: 9/9 killed (M20 N/A).

O mutante de `EventMode::ALWAYS` ➔ `ON_VALUE_CHANGE` em `VipeeVehicleHardware.cpp:174` não foi injetado: exige build AOSP (proibido aqui). O gate `test-vhal.sh rate` (`:137`) o mataria, já que velocidade constante deixaria de gerar eventos.

### Live evidence (emulador, 2026-11-02, ciclo 2)

- `tools/emu/test-vhal.sh`: `absent`, `live` OK; `rate: OK (332 eventos em 5 s, assinatura a 5 Hz)`.
- `tools/emu/test-e2e.sh`: latência das 4 propriedades juntas 62–134 ms (pior 134 ms, adb incluído; o autor mediu 152 ms); dado velho das 4 em 577 ms (autor: 556 ms) com `STALE` de cada uma; mapas OK; sem `avc: denied`.
- Histórico (só velocidade): T17 pior 48 ms e dado velho 537 ms; ciclo 1 pior 42 ms e 554 ms.

### Gate

- Unit: `cmake --preset default && cmake --build --preset default && ctest --preset default` ➔ 81 passed, 0 failed (4 testes de dizimação removidos com o requisito, 4 novos: 3 no ingest, 1 no núcleo).
- ROS: `tools/jazzy/run.sh /bridge/tools/jazzy/test-ros.sh` ➔ 2/2 (`bridge_node_rostest` com 4 casos, `unreadable_map_test`).

### Observações (não bloqueiam)

1. `core/test/bridge_test.cpp:471` usa `log_.of(...).back()` sem checar vazio; o mutante M7 morre por comportamento indefinido (SEGFAULT), não por asserção. Trocar por `ASSERT_FALSE(log_.of(prop::kCurrentGear).empty())` antes.
2. O dado velho das 4 no e2e fecha a 577 ms contra o limite de 600 ms (500 do mapa + até 50 do tick + adb). Margem curta, já registrada como medida no host; se o teste oscilar, medir no device em vez de aumentar o limite.
3. `initRclcpp` refaz o init para qualquer exceção do primeiro, não só a do arquivo de parâmetros; outra causa falha de novo no segundo init e sai com `fatal`, o que segue fail-loud.

### Requirement Traceability Update (ciclo 2)

| Requirement | Ciclo 1 | Ciclo 2 |
| ----------- | ------- | ------- |
| CTS-10 | ⚠️ só velocidade | ✅ Verified (4 propriedades, pior 134 ms) |
| CTS-19 | ⚠️ spec-precision gap | ✅ Verified (spec emendado, M7 morto) |
| CTS-23 | ❌ Needs Fix | ✅ Verified (`unreadable_map_test`) |
| Demais | ✅ Verified | ✅ Verified |

**Overall**: ✅ Ready.

---

# Ciclo 1 (histórico)

**Date**: 2026-11-02
**Spec**: `.specs/features/ct-speed-slice/spec.md`
**Diff range (ciclo 1)**: `a369c5b^..a9eab54` (T1–T18, fases 1–4)
**Verifier**: sub-agente independente (autor ≠ verificador)

**Veredito do ciclo 1**: reprovado, commit `f72c91c`; gaps fechados no ciclo 2 (acima)

Motivo: 3 mutantes de comportamento sobrevivem à suíte unit (um deles só morre no teste de emulador), o ramo "mapa ilegível" do CTS-23 não tem evidência e o processo faz o oposto do spec, e os Success Criteria 1 e 2 são checados ponta a ponta em 1 das 4 propriedades. O núcleo (`core/`) está bem coberto: 22 de 23 mutantes do núcleo, do sink e do ingest morrem.

---

## Task Completion

| Task | Status | Notes |
| ---- | ------ | ----- |
| T1–T12 | ✅ Done | um commit por task (`a369c5b`..`978a2f3`) |
| T13 | ✅ Done | ⚠️ desvio registrado: código do VHAL das T13–T15 num commit só (`e2a7f4c`). Julgado: aceitável, porque o `Android.bp` só compila o hardware inteiro e a lógica testável já tinha entrado antes com testes (`0614bf5`). Custa a rastreabilidade por task do VHAL, não o comportamento |
| T14 | ✅ Done | `cb222eb` registra o gate `test-vhal.sh live` |
| T15 | ✅ Done | ⚠️ desvio registrado: o teste usa a assinatura de 5 Hz do `CarService`, não 10 Hz. Julgado: aceitável como gate, mas a medição (23 eventos em 5 s a 5 Hz = 4,6 Hz) mostra a dizimação abaixo da taxa assinada; ver Gap 4 |
| T16 | ✅ Done | `3d2e1cb` |
| T17 | ✅ Done | ⚠️ latência e dado velho só com o mapa do CAT793F, e só na velocidade; ver Gap 3 |
| T18 | ✅ Done | `a9eab54` |

---

## Spec-Anchored Acceptance Criteria

| Criterion | Spec-defined outcome | `file:line` + assertion | Result |
| --------- | -------------------- | ----------------------- | ------ |
| CTS-01 `speed = 36.0` | `PERF_VEHICLE_SPEED = 10.0` m/s, `AVAILABLE` | `core/test/bridge_test.cpp:93-94` `EXPECT_EQ(u.status, kAvailable)`, `EXPECT_FLOAT_EQ(u.float_value, 10.0f)`; `core/test/translate_test.cpp:16`; ROS real: `ros/vipee_bridge/test/bridge_node_rostest.cpp:138-139`; emulador: `tools/emu/test-vhal.sh:82` | ✅ PASS |
| CTS-02 `speed = -18.0` | `-5.0` m/s qualquer marcha; `-0.0` ➔ `0.0` | `core/test/bridge_test.cpp:102,104` `EXPECT_FLOAT_EQ(..., -5.0f)` com marcha 2 e -1; `:111-112` `signbit` falso; `translate_test.cpp:22,28-29` | ✅ PASS |
| CTS-03 `CURRENT_GEAR` genérica | `<0` ➔ `0x0002`, `0` ➔ `0x0001`, `n` ➔ `GEAR_n` (`3` ➔ `0x0040`) | `core/test/bridge_test.cpp:119,122,124`; `translate_test.cpp:51-53,58,66` (1..9) | ✅ PASS |
| CTS-04 alavanca CAT793F | `80` ➔ `0x0004`, `68` ➔ `0x0008`, os 6 códigos | `core/test/bridge_test.cpp:294,297`; `translate_test.cpp:98-103`; ROS real `bridge_node_rostest.cpp:141`; emulador `tools/emu/test-e2e.sh:90` | ✅ PASS |
| CTS-05 seleção derivada | R/N/D | `core/test/bridge_test.cpp:131,134,136`; `translate_test.cpp:77-80` | ✅ PASS |
| CTS-06 derivada sem P | nunca `0x0004` | `core/test/bridge_test.cpp:143-147` (gear -12..12); `translate_test.cpp:87` | ✅ PASS |
| CTS-07 freio de mão | `PARKING_BRAKE_ON = handbrake` | `core/test/bridge_test.cpp:381,385` `int_value` 1 e 0 | ✅ PASS |
| CTS-08 sem evento repetido (ON_CHANGE) | nenhum evento novo se o valor traduzido não muda | `core/test/bridge_test.cpp:156-157,305,394` (contagem exata de publicações); VHAL usa `EventMode::ON_VALUE_CHANGE` (`android/vhal/VipeeVehicleHardware.cpp:176`), que no AOSP compara valor e status (`VehiclePropertyStore.cpp:155`) | ✅ PASS |
| CTS-09 carimbo na chegada | `elapsedRealtimeNano()` da chegada | `core/test/bridge_test.cpp:95,165-167,295,382` (`timestamp_ns == now_ns` passado); relógio `CLOCK_BOOTTIME` em `ros/vipee_bridge/src/bridge_node.cpp:26-30`; piso pós-watchdog `android/vhal/test/vhal_ingest_test.cpp:72` | ✅ PASS (o piso eleva o carimbo após um watchdog; desvio documentado e necessário pelo `OnChangeCarPropertyEventTracker.java:50`) |
| CTS-10 latência ≤ 200 ms | valor novo legível no VHAL em ≤ 200 ms | `tools/emu/test-e2e.sh:71` `[ "$worst" -le 200 ]`. T17: pior 48 ms em 10 leituras; esta verificação: pior 42 ms (26–42 ms). Medido no host com adb incluído (limite superior) | ⚠️ PASS só para `PERF_VEHICLE_SPEED` e só com o mapa CAT793F (Gap 3) |
| CTS-11 sem dado desde o início | 4 `UNAVAILABLE` | `core/test/bridge_test.cpp:77,80` (status e `NO_DATA`); VHAL `vhal_ingest_test.cpp:58` (desconexão marca); emulador `tools/emu/test-vhal.sh:89` (`absent`: 4 `NOT_AVAILABLE`) | ✅ PASS |
| CTS-12 telemetria velha | velocidade, seleção e marcha `UNAVAILABLE` após 500 ms | `core/test/bridge_test.cpp:176` (nada em 500 ms), `:181,183` (`UNAVAILABLE`/`STALE` em 501 ms, 3 propriedades); ROS real `bridge_node_rostest.cpp:184`; emulador `test-e2e.sh:79-83` (T17: 537 ms; agora: 554 ms, ≤ 600) | ✅ PASS (e2e só checa a velocidade; Gap 3) |
| CTS-13 controle velho | `PARKING_BRAKE_ON` `UNAVAILABLE` | `core/test/bridge_test.cpp:433,435-436` (500 ms ainda AVAILABLE, 501 ms STALE) | ✅ PASS |
| CTS-14 alavanca velha | `GEAR_SELECTION` `UNAVAILABLE`, independente da telemetria | `core/test/bridge_test.cpp:324-327,340-341` | ✅ PASS |
| CTS-15 volta a `AVAILABLE` | novo valor com `AVAILABLE` | `core/test/bridge_test.cpp:194-199,329-330,441-442`; emulador `test-vhal.sh:119` (resync após SIGCONT) | ✅ PASS |
| CTS-16 velocidade inválida | `ERROR` para NaN, inf, `80.1` no CAT793F; `80.0` aceito | `core/test/bridge_test.cpp:206-214,220,222`; `translate_test.cpp:33-47`; `get` ➔ `INTERNAL_ERROR` em `VipeeVehicleHardware.cpp:224-225` | ✅ PASS (sem teste de emulador para `ERROR` ➔ `INTERNAL_ERROR`) |
| CTS-17 marcha/alavanca desconhecida | `UNAVAILABLE`/`UNKNOWN_GEAR` | `core/test/bridge_test.cpp:239-242,247-248,349-350`; `translate_test.cpp:72-73,108-109` | ✅ PASS |
| CTS-18 `id` ambíguo | `UNAVAILABLE`/`AMBIGUOUS_SOURCE` até uma janela inteira com um `id` | `core/test/bridge_test.cpp:402-404,408,410`; `:419,422` (troca após pausa não é ambígua) | ✅ PASS |
| CTS-19 log de transição | logcat tag `vipee_bridge` com propriedade, status, motivo | `core/test/bridge_test.cpp:262-269` (sequência exata); formato `ros/vipee_bridge/src/main.cpp:159-160`; logcat real `test-e2e.sh:81` (`0x11600207 UNAVAILABLE STALE`) | ⚠️ PASS com spec-precision gap: mudança de motivo com o mesmo status (ex.: `UNKNOWN_GEAR` ➔ `STALE`) não é exigida pelo spec, mas o contrato do `Logger` (`core/include/vipee/bridge.h:43`) promete; mutante M7 sobrevive |
| CTS-20 campos do mapa | versão, namespace, fonte, escala, máxima, limite, fonte da seleção | `core/test/vehicle_map_test.cpp:10-106`; leitura dos parâmetros `ros/vipee_bridge/src/bridge_node.cpp:32-65` exercitada por `bridge_node_rostest.cpp:116,147` | ✅ PASS |
| CTS-21 mapa pelo veículo | `commander` assina `/commander/telemetry` | `bridge_node_rostest.cpp:147,158`; emulador `test-e2e.sh:94-95` (`vendor.vipee.vehicle=commander` ➔ DRIVE) | ✅ PASS |
| CTS-22 mapas no repo | `cat793f` 80 km/h alavanca; `commander` 220 km/h derivada; 500 ms | `maps/cat793f.yaml:8,12,16`, `maps/commander.yaml:8,12,16`; `core/test/bridge_test.cpp:369,373` | ✅ PASS |
| CTS-23 mapa inválido | recusa, `MAP_INVALID` no logcat, 4 `UNAVAILABLE` | versão/campo/propriedade: `core/test/bridge_test.cpp:279,281`, `vehicle_map_test.cpp:18,75,87`, `bridge_node_rostest.cpp:194,208,215`. **Mapa ilegível: sem evidência.** YAML ilegível faz o `rclcpp::init` lançar e o `main` sair com `fatal:` (`ros/vipee_bridge/src/main.cpp:172,181-188`), sem `MAP_INVALID` e com o init reiniciando o serviço | ❌ GAP (Gap 2) |

**Status**: ❌ 1 AC com ramo sem evidência (CTS-23), ⚠️ 2 com cobertura parcial (CTS-10, CTS-19).

---

## Discrimination Sensor

Worktree descartável em `scratchpad/verify-wt` (`git worktree add --detach`), suíte `ctest --preset default -L unit` (81 testes, verde antes). Worktree removida; `git status --porcelain` do repo real igual ao baseline.

| Mutation | File:line | Description | Killed? |
| -------- | --------- | ----------- | ------- |
| M1 | `core/src/bridge.cpp:75` | `isStale` `>` ➔ `>=` | ✅ Killed |
| M2 | `core/src/bridge.cpp:64` | telemetria velha derruba a seleção da alavanca | ✅ Killed |
| M3 | `core/src/bridge.cpp:66-68` | remove o dado velho da alavanca | ✅ Killed |
| M4 | `core/src/bridge.cpp:103` | ON_CHANGE republica valor igual | ✅ Killed |
| M5 | `core/src/bridge.cpp:51` | `AMBIGUOUS_SOURCE` nunca se recupera | ✅ Killed |
| M6 | `core/src/bridge.cpp:45-46` | troca de `id` após pausa vira ambígua | ✅ Killed |
| M7 | `core/src/bridge.cpp:95` | log só em mudança de status, não de motivo | ❌ Survived (spec-equivalente; ver CTS-19) |
| M8 | `core/src/bridge.cpp:28` | velocidade inválida `UNAVAILABLE` em vez de `ERROR` | ✅ Killed |
| M9 | `core/src/bridge.cpp:38` | mapa derivado aceita a alavanca | ✅ Killed |
| M10 | `core/src/bridge.cpp:54-55` | carimbo do freio = chegada − 1 ns | ✅ Killed |
| M11 | `core/src/translate.cpp:13` | limite de velocidade exclusivo | ✅ Killed |
| M12 | `core/src/translate.cpp:31` | neutro derivado vira DRIVE | ✅ Killed |
| M13 | `core/src/translate.cpp:21` | aceita `gear = 10` | ✅ Killed |
| M14 | `core/src/translate.cpp:17` | mantém `-0.0` | ✅ Killed |
| M15 | `core/src/vehicle_map.cpp:41` | aceita `stale_ms = 0` | ✅ Killed |
| M16 | `android/vhal/include/vipee/vhal_ingest.h:88` | watchdog `>` ➔ `>=` | ✅ Killed |
| M17 | `android/vhal/include/vipee/vhal_ingest.h:150` | remove o piso de timestamp | ✅ Killed |
| M18 | `android/vhal/include/vipee/vhal_ingest.h:149` | update válido não alimenta o watchdog | ❌ Survived |
| M19 | `android/vhal/include/vipee/vhal_ingest.h:156` | dizimação engole mudança de status da velocidade | ✅ Killed |
| M20 | `android/vhal/include/vipee/vhal_ingest.h:168` | marcador do watchdog não zera o status da velocidade (resync pode ser dizimado) | ❌ Survived |
| M21 | `android/vhal/include/vipee/vhal_ingest.h:140-141` | `CURRENT_GEAR` descartada como propriedade desconhecida | ❌ Survived na unit (morreria em `tools/emu/test-vhal.sh:99`, só no emulador) |
| M22 | `android/vhal/include/vipee/vhal_ingest.h:145` | aceita status 3 | ✅ Killed |
| M23 | `android/vhal/include/vipee/vhal_ingest.h:106` | dizima ao dobro da taxa | ✅ Killed |
| M24 | `android/vhal/include/vipee/vhal_ingest.h:132` | heartbeat não alimenta o watchdog | ✅ Killed |
| M25 | `ros/vipee_bridge/src/socket_sink.cpp:89` | não reenvia o estado ao reconectar | ✅ Killed |

**Sensor depth**: expandido (P0: dado entregue ao dashboard, fail-loud)
**Resultado (ciclo 1)**: 21/25 killed, 4 survived - reprovado

---

## API correctness (AOSP `android-16.0.0_r4` em `~/aosp`, context7 rclcpp/AAOS)

- `IVehicleHardware`: assinaturas de `getValues`, `setValues`, `subscribe(SubscribeOptions)`, `unsubscribe`, `updateSampleRate`, `checkHealth`, `dump` batem com `hardware/include/IVehicleHardware.h:109-253`. ✅
- `getValues`: `UNAVAILABLE` ➔ `StatusCode::NOT_AVAILABLE`, `ERROR` ➔ `INTERNAL_ERROR`, valor só com `AVAILABLE` (`VipeeVehicleHardware.cpp:222-228`) segue `VehiclePropertyStatus.aidl:35-37` ("for get property results, VehiclePropertyStatus should always be AVAILABLE"). ✅
- `VehiclePropertyStore::writeValue(value, updateStatus=true, EventMode)`: `ON_VALUE_CHANGE` compara valor e status ignorando o carimbo (`VehiclePropertyStore.cpp:155-156`); `NEVER` atualiza sem evento, uso previsto pelo AOSP para dizimar contínua (`VehiclePropertyStore.h:64-73`); o store recusa carimbo mais velho (`VehiclePropertyStore.cpp:145-147`), o que o piso de `vhal_ingest.h` respeita. ✅
- Taxa da contínua: o contrato pede "a sample rate of 10hz must generate **at least** 10 property change events per second" (`IVehicleHardware.h:173-174`). A dizimação agenda `next = t_emitido + período` (`vhal_ingest.h:157`), então com entrada a 15 ms o intervalo real arredonda para cima: 4,6 Hz medidos a 5 Hz assinados (`test-vhal.sh rate`, 23 eventos em 5 s, aqui e na T15). ⚠️ Gap 4.
- rclcpp Jazzy: `get_node_parameters_interface()->get_parameter_overrides()` (`bridge_node.cpp:71`) lê o YAML sem declarar parâmetros; válido, mas os parâmetros não aparecem em `ros2 param`. QoS das assinaturas (reliable, depth 5/1/2) bate com `docs/sim-contract.md:26-29`. `SingleThreadedExecutor` + `create_wall_timer(50 ms)` como no design. ✅

---

## Live evidence (emulador `emulator-5554`, 2026-11-02, por este Verifier)

- `tools/emu/test-vhal.sh`: `absent: OK`, `live: OK`, `rate: OK (23 eventos em 5 s a 5 Hz)`.
- `tools/emu/test-e2e.sh`: latência 26–42 ms (pior 42 ms, com adb) em 10 leituras de 2 a 20 m/s; dado velho 554 ms com `STALE` no logcat; mapas OK. Sem `avc: denied` dos dois domínios.
- Medição registrada na T17 pelo autor: pior 48 ms em 10 leituras, no host com adb incluído; dado velho 537 ms. As duas medições cabem nos limites (200 ms, 600 ms).

---

## Gate Check

- **Gate command**: `cmake --preset default && cmake --build --preset default && ctest --preset default` (worktree limpa em `a9eab54`)
- **Result**: 81 passed, 0 failed, 0 skipped (todos rotulados `unit`)
- **Test count before feature**: 0 (repo sem testes)
- **Test count after feature**: 81 unit + 4 de integração ROS (`bridge_node_rostest.cpp`, contêiner; não rodados aqui) + `test-vhal.sh`/`test-e2e.sh` (rodados, verdes)
- **Failures**: nenhuma

---

## Code Quality

| Principle | Status |
| --------- | ------ |
| Minimum code / surgical / no scope creep | ✅ |
| Matches patterns (CalibDroid CMake, AOSP `EmulatedVehicleService`) | ✅ |
| Spec-anchored outcome check | ⚠️ CTS-10/SC1-2 no e2e só na velocidade |
| Per-layer coverage (domínio 1:1) | ✅ núcleo; ⚠️ `vhal_ingest.h` sem teste para `CURRENT_GEAR` e para update alimentando o watchdog |
| Every test maps to a requirement | ✅ |
| Guidelines: `AGENTS.md` §3 (fail-loud, núcleo sem ROS/Android), §4 Doxygen | ⚠️ fail-loud de mapa ilegível não segue o caminho `MAP_INVALID` |

---

## Edge Cases

- [x] Processo ROS reinicia ➔ `UNAVAILABLE` até a primeira mensagem (`bridge_test.cpp:73-87`, `vhal_ingest_test.cpp:55-61`)
- [x] Simulador pausa ➔ `UNAVAILABLE` após o limite (`bridge_test.cpp:171-186`, `test-e2e.sh` critério 2)
- [x] `speed = -0.0` ➔ `0.0` (`bridge_test.cpp:109-113`)
- [x] `speed = 80.0` no CAT793F aceito (`bridge_test.cpp:209-210`)
- [x] `gear = -2` ➔ `GEAR_REVERSE` (`bridge_test.cpp:121-122`)

---

## Fix Plans

### Fix 1 (Major): VHAL sem teste unit para `CURRENT_GEAR` e para update válido alimentando o watchdog (M18, M21)
- **Root cause**: `vhal_ingest_test.cpp` só usa velocidade, seleção e freio; nenhum teste manda só updates (sem heartbeat) e checa o watchdog.
- **Fix task**: em `android/vhal/test/vhal_ingest_test.cpp`, (a) as 4 propriedades da fatia dão `kWrite` (parametrizado nos 4 ids); (b) `onConnect(0)`, update válido em 250 ms, `expire(400 ms)` falso e `expire(551 ms)` verdadeiro.
- **Done when**: M18 e M21 reaplicados em scratch fazem a suíte falhar.

### Fix 2 (Major): mapa ilegível não segue o caminho `MAP_INVALID` (CTS-23)
- **Root cause**: o YAML vai por `--params-file`; erro de parse lança dentro de `rclcpp::init` e o `main` sai (`ros/vipee_bridge/src/main.cpp:172,181-188`), antes de existir `Bridge`. O VHAL fica `UNAVAILABLE` por falta de par, mas o logcat não tem `MAP_INVALID` e o init entra em laço de reinício.
- **Fix task**: no `main`, se o `rclcpp::init` falhar por causa do arquivo de parâmetros, iniciar sem ele (ou ler o caminho por variável/propriedade e carregar com tratamento próprio) e construir o `Bridge` com mapa inválido, para logar `MAP_INVALID` e manter o processo vivo; teste de integração em `bridge_node_rostest.cpp` com YAML corrompido. Alternativa: emenda no spec aceitando "falha alta com saída do processo" para mapa ilegível, com nova decisão.
- **Done when**: com um YAML ilegível, o logcat mostra as 4 `UNAVAILABLE MAP_INVALID` e o processo segue vivo (ou o spec é emendado).

### Fix 3 (Minor): Success Criteria 1 e 2 checados só na velocidade (CTS-10, CTS-12)
- **Root cause**: `tools/emu/test-e2e.sh:63-84` lê só `PERF_VEHICLE_SPEED`; o SC1 pede as 4 propriedades em 10 leituras e o SC2 as 4 `UNAVAILABLE` em ≤ 600 ms. O estado `"telemetry": false` não para alavanca nem controle.
- **Fix task**: no e2e, variar também `gear`, `lever` e `handbrake` nas 10 leituras e medir cada propriedade; no critério 2 parar as três fontes e exigir as 4 `NOT_AVAILABLE` em ≤ 600 ms.
- **Done when**: `test-e2e.sh` falha se qualquer uma das 4 propriedades passar dos limites.

### Fix 4 (Minor): dizimação entrega menos que a taxa assinada (M20 e contrato `IVehicleHardware`)
- **Root cause**: `next_speed_ns_ = t + período` (`vhal_ingest.h:157`) acumula o atraso de grade (4,6 Hz a 5 Hz). E o marcador do watchdog/desconexão zera `last_speed_status_` sem teste (M20 sobrevive); sem ele o primeiro valor do resync pode ser dizimado.
- **Fix task**: avançar `next_speed_ns_` em grade (`next += período`, re-ancorando se ficar para trás) ou emitir com tolerância de meia amostra; teste em `vhal_ingest_test.cpp` exigindo ≥ 10 eventos/s a 10 Hz com entrada a 15 ms; teste: velocidade AVAILABLE, `expire`, reconexão, primeiro update AVAILABLE dentro do período ➔ `emit == true`.
- **Done when**: M20 morre e a contagem a 10 Hz fica ≥ 10 por segundo. O cliente do AOSP descarta eventos a menos de um período (`ContCarPropertyEventTracker.java:127`): conferir que a grade não gera intervalos menores que o período.

### Fix 5 (Cosmetic / spec): log de mudança só de motivo (M7)
- **Root cause**: CTS-19 fala em mudança de status; o `Logger` promete status ou motivo, e nenhum teste fixa a mudança só de motivo.
- **Fix task**: decidir no spec; se o log de motivo vale, teste em `bridge_test.cpp` com `gear = 10` (`UNKNOWN_GEAR`) seguido de silêncio (`STALE`) exigindo duas entradas.

---

## Requirement Traceability Update

| Requirement | Previous Status | New Status |
| ----------- | --------------- | ---------- |
| CTS-01..CTS-09, CTS-11..CTS-18, CTS-20..CTS-22 | Implemented | ✅ Verified |
| CTS-10 | Implemented | ⚠️ Verified só na velocidade (Fix 3) |
| CTS-19 | Implemented | ⚠️ Verified, spec-precision gap (Fix 5) |
| CTS-23 | Implemented | ❌ Needs Fix (Fix 2) |

---

## Summary

**Overall**: ❌ Not Ready

**Spec-anchored check**: 20/23 ACs batem com o spec; 1 gap (CTS-23 ilegível), 2 parciais (CTS-10, CTS-19)
**Sensor**: 21/25 mutantes mortos (4 sobreviventes: M7, M18, M20, M21)
**Gate**: 81 passed, 0 failed; `test-vhal.sh` e `test-e2e.sh` verdes no emulador

**What works**: tradução, dado velho, fonte ambígua e mapa no núcleo, todos com testes que matam os mutantes; sink com resync; VHAL fiel à API do AOSP 16; latência medida 42–48 ms (≤ 200) e dado velho 537–554 ms (≤ 600).

**Next steps**: Fix 1 e Fix 2, depois Fix 3 e Fix 4; nova verificação (iteração 1 de 3).
