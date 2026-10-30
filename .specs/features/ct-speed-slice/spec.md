# ct-speed-slice Specification

## Problem Statement

O dashboard AAOS precisa de velocidade, marcha e freio de mão do simulador para decidir quando pedir câmera (ré obrigatória por legislação, solicitação por velocidade) e para o `CarDrivingStateService` derivar o estado de condução. Hoje o processo ROS da ponte roda no Android (AD-011/AD-012) mas só assina um tópico de teste. Esta fatia é o primeiro caminho completo simulador ➔ VHAL, e a base do mapa tabelado por veículo que as outras features estendem.

## Goals

- [ ] `PERF_VEHICLE_SPEED`, `GEAR_SELECTION`, `CURRENT_GEAR` e `PARKING_BRAKE_ON` lidos no emulador AAOS 16 (`cmd car_service get-property-value`) refletem o simulador, com o valor novo visível em até 200 ms após a publicação.
- [ ] Sinal ausente, velho ou inválido aparece como `UNAVAILABLE` ou `ERROR` no VHAL, nunca como valor plausível.
- [ ] O que varia por veículo (velocidade máxima, limite de dado velho, fonte e tabela da alavanca) vem de um mapa por veículo; trocar de veículo não muda código.

## Out of Scope

| Feature | Reason |
| ------- | ------ |
| Portas e retrovisores (`DOOR_POS`, `MIRROR_FOLD`) | Dependem dos pedidos P1/P2 ao simulador; feature própria |
| Câmeras e HAL EVS | Feature própria (vídeo) |
| Build arm64 e validação no RPi5 | Etapa de validação seguinte; esta fatia fecha no emulador |
| Propriedades de plataforma (`AP_POWER_STATE_*`, etc.) com fonte no simulador | O simulador não tem essa informação; continuam servidas como hoje no emulador (ver Assumptions) |
| Alavanca dos veículos genéricos (Commander etc.) | O `generic_logic` recebe só a marcha numérica em `SirvVehicleControl.gear`, sem P nem D; depende do pedido P7 |
| `/<ns>/vehicle_info` publicado pelo simulador | Pedido P6 do contrato; até lá os dados do veículo ficam no mapa da ponte |
| Escrita do Android para o simulador | MVP é só leitura |
| `dontaudit` do `proc_net` | Não muda comportamento; fica para quando o disco permitir outro `m selinux_policy` |

---

## Assumptions & Open Questions

| Assumption / decision | Chosen default | Rationale | Confirmed? |
| --------------------- | -------------- | --------- | ---------- |
| Fonte de velocidade e marcha engatada | `/<ns>/telemetry` (`SirvVehicleTelemetry.speed`, `.gear`), `ns` vindo do mapa do veículo (`cat793f` no demo) | Contrato v0.2 §3 | y |
| Fonte do freio de mão | `/apply_control_to_vehicle` (`SirvVehicleControl.handbrake`), aceito de qualquer `id`; dois `id` diferentes dentro do limite de dado velho tornam a fonte ambígua | Único sinal existente (contrato v0.2 §3.3). O `id` é o uid que o UE5 atribui ao spawnar (`LV_identifier`, `SirvVehicleSimulator.cpp:1363`), não um valor fixo, e não aparece em outro tópico; o único publicador é o vehiclesim do ego (`SirvVehicleSimulator.cpp:151`). Substituído pelo estado em `/<ns>/` quando o P3 chegar | y |
| Velocidade | `PERF_VEHICLE_SPEED = speed / 3,6`, com o sinal do simulador, independente da marcha | `speed` é `GetForwardSpeed() * 0.036` do Chaos (`SirvWheeledVehicle.cpp:258`): km/h com sinal, negativo andando para trás; mesma convenção do `PERF_VEHICLE_SPEED` no `VehicleProperty.aidl` | y |
| Marcha engatada | `telemetry.gear` é a marcha engatada da transmissão Chaos (`GetCurrentGear()`, `SirvWheeledVehicle.cpp:256`), igual para todos os veículos: `< 0` ré, `0` neutro, `1..n` à frente; não existe P na física | Lido do código do simulador | y |
| `GEAR_SELECTION` no CAT793F | Vem da alavanca: `/<ns>/vehicle_control`, `CAT793FVehicleControl.transmission` (int8 em ASCII): `'P'` (80) ➔ `GEAR_PARK`, `'R'` (82) ➔ `GEAR_REVERSE`, `'N'` (78) ➔ `GEAR_NEUTRAL`, `'D'` (68) ➔ `GEAR_DRIVE`, `'1'` (49) ➔ `GEAR_1`, `'2'` (50) ➔ `GEAR_2` | Lido do `cat793f_driving_logic.cpp:739-782`; publicado pelo `cat793f_analogic_bridge` (`SirvAnalogicBridge.cpp:74`) a partir do cockpit | y |
| `GEAR_SELECTION` nos veículos genéricos | Derivada da engatada: ré ➔ `GEAR_REVERSE`, neutro ➔ `GEAR_NEUTRAL`, qualquer marcha à frente ➔ `GEAR_DRIVE`; `GEAR_PARK` nunca, até o pedido P7. Todo automático tem P (Commander: P R N D, 9 marchas), mas o `generic_logic` só recebe a marcha numérica | `generic_logic.cpp`; informação do usuário (2026-10-30) | y |
| Freio de mão e P | Independentes na ponte: `PARKING_BRAKE_ON` vem sempre de `handbrake`. No CAT793F o próprio simulador puxa o freio de mão quando a alavanca vai para P (`cat793f_driving_logic.cpp:776`), então as duas propriedades ficam coerentes sem regra na ponte. No Commander e nos veículos comuns são independentes (D com freio de mão puxado é possível) | Informação do usuário; código do simulador | y |
| Código de alavanca desconhecido | `UNAVAILABLE` com motivo `UNKNOWN_GEAR`, mesmo o simulador tratando código desconhecido como P | Fail-loud: a ponte não adivinha a alavanca | y |
| Taxa da alavanca do CAT793F | `/cat793f/vehicle_control` sai a cada leitura do cockpit (`/pwm_input`); vale o mesmo limite de dado velho. Sem cockpit, `GEAR_SELECTION` fica `UNAVAILABLE` no CAT793F | `SirvAnalogicBridge.cpp:281`; confirmar com `ros2 topic hz` | n |
| Limite de dado velho | 500 ms por padrão, configurável por veículo no mapa | Telemetria sai a cada passo de 15 ms; 500 ms tolera pausas curtas sem esconder um simulador parado | y |
| Freio de mão publicado periodicamente | `/apply_control_to_vehicle` sai a cada passo de simulação, então vale o mesmo limite | Lido do `SirvVehicleSimulator.cpp` (timer de 15 ms); confirmar com `ros2 topic hz` no primeiro teste integrado | n |
| Velocidade máxima válida | Por veículo no mapa: CAT793F 80 km/h, Commander 220 km/h; acima disso ou `NaN`/`inf` ➔ `ERROR` | CAT793F tem máxima de projeto próxima de 60 km/h; Commander ~200 km/h; folga sem aceitar lixo | n |
| Latência | ≤ 200 ms da publicação no host até o VHAL do emulador | Provisório; ajustar quando houver medição real | y |
| Carimbo do valor no VHAL | `elapsedRealtimeNano()` na chegada | AD-010 | y |
| Propriedades de plataforma | O `CarService` do emulador continua recebendo `AP_POWER_STATE_*` e as demais propriedades obrigatórias como hoje; a ponte só assume as 4 propriedades desta fatia | AD-009 pede declarar só o que o simulador alimenta, mas o `CarService` não sobe sem as de plataforma (STATE). Como conciliar é decisão de Design | n |
| Ligação processo ROS ➔ VHAL | Decidida no Design como AD nova (AIDL vendor, socket local ou VHAL hospedando o nó) | AD-012 deixou o IPC em aberto; não muda o comportamento visível | y |
| Build AOSP | Esta fatia exige ao menos um build de VHAL/sepolicy no `~/aosp`, com aprovação explícita antes (AGENTS.md §5) e conferência de disco do C: | Cada build custa ~30 min e até ~28 GB de VHDX | y |

**Open questions:** none - as perguntas 1 e 2 do contrato v0.2 §5 foram respondidas pelo código do simulador; os itens `n` acima são defaults de valor, ajustáveis só no mapa.

---

## User Stories

### P1: Velocidade, marcha e freio de mão do simulador no VHAL ⭐ MVP

**User Story**: Como desenvolvedor do dashboard, quero que o VHAL do emulador mostre a velocidade, a marcha e o freio de mão do simulador para que o dashboard reaja ao veículo simulado como reagiria ao real.

**Why P1**: É a primeira fatia vertical da ponte; sem ela nenhuma função do dashboard roda acoplada ao simulador.

**Acceptance Criteria**:

1. WHEN uma `SirvVehicleTelemetry` válida chega em `/<ns>/telemetry` com `speed = 36.0` THEN a ponte SHALL publicar `PERF_VEHICLE_SPEED = 10.0` m/s com status `AVAILABLE`.
2. WHEN uma `SirvVehicleTelemetry` válida chega com `speed = -18.0` THEN a ponte SHALL publicar `PERF_VEHICLE_SPEED = -5.0` m/s, qualquer que seja a marcha.
3. WHEN uma `SirvVehicleTelemetry` válida chega com `gear` entre `-9` e `9` THEN a ponte SHALL publicar `CURRENT_GEAR` pela tabela genérica: `gear < 0` ➔ `GEAR_REVERSE` (`0x0002`), `gear = 0` ➔ `GEAR_NEUTRAL` (`0x0001`), `gear = n` com `1 ≤ n ≤ 9` ➔ `GEAR_n` (ex.: `3` ➔ `GEAR_3` = `0x0040`).
4. WHERE o mapa do veículo define a alavanca do CAT793F, WHEN uma `CAT793FVehicleControl` chega em `/<ns>/vehicle_control` com `transmission` na tabela THEN a ponte SHALL publicar `GEAR_SELECTION` pela tabela (ex.: `80` (`'P'`) ➔ `GEAR_PARK` = `0x0004`, `68` (`'D'`) ➔ `GEAR_DRIVE` = `0x0008`).
5. WHERE o mapa do veículo deriva a seleção da marcha engatada, WHEN `CURRENT_GEAR` é traduzida THEN a ponte SHALL publicar `GEAR_SELECTION` = `GEAR_REVERSE` para ré, `GEAR_NEUTRAL` para neutro e `GEAR_DRIVE` para qualquer marcha à frente.
6. WHERE o mapa do veículo deriva a seleção da marcha engatada, the ponte SHALL nunca publicar `GEAR_SELECTION = GEAR_PARK`.
7. WHEN uma `SirvVehicleControl` chega em `/apply_control_to_vehicle` THEN a ponte SHALL publicar `PARKING_BRAKE_ON` igual a `handbrake`.
8. WHEN o valor traduzido de uma propriedade ON_CHANGE (`GEAR_SELECTION`, `CURRENT_GEAR`, `PARKING_BRAKE_ON`) não muda entre duas mensagens THEN a ponte SHALL não gerar novo evento de mudança para os assinantes do VHAL.
9. The ponte SHALL carimbar cada valor entregue ao VHAL com `elapsedRealtimeNano()` no instante da chegada da mensagem ROS.
10. WHEN a mensagem é publicada no host THEN o valor novo SHALL estar legível no VHAL do emulador em até 200 ms.

**Independent Test**: Publicar telemetria e controle fake (contêiner Jazzy, `sirv_msgs`) para o emulador e ler as 4 propriedades com `cmd car_service get-property-value`; o núcleo também é testado sem ROS e sem Android com fonte e sink fakes, com os mapas do CAT793F e do Commander.

---

### P1: Fail-loud para sinal ausente, velho ou inválido ⭐ MVP

**User Story**: Como desenvolvedor do dashboard, quero ver a propriedade indisponível quando o simulador não entrega dado confiável para nunca tomar decisão em cima de valor inventado ou congelado.

**Why P1**: Princípio normativo do projeto (AGENTS.md §3, AD-009/AD-010); valor congelado é o pior caso.

**Acceptance Criteria**:

1. WHILE nenhuma mensagem válida de um tópico de origem chegou desde o início do processo a ponte SHALL reportar as propriedades daquele tópico com status `UNAVAILABLE`.
2. IF passa o limite de dado velho do mapa (500 ms nos mapas do demo) sem mensagem válida em `/<ns>/telemetry` THEN a ponte SHALL reportar `PERF_VEHICLE_SPEED`, `GEAR_SELECTION` e `CURRENT_GEAR` com status `UNAVAILABLE`.
3. IF passa o limite de dado velho sem `SirvVehicleControl` THEN a ponte SHALL reportar `PARKING_BRAKE_ON` com status `UNAVAILABLE`.
4. WHERE o mapa do veículo define a alavanca do CAT793F, IF passa o limite de dado velho sem mensagem válida em `/<ns>/vehicle_control` THEN a ponte SHALL reportar `GEAR_SELECTION` com status `UNAVAILABLE`, independente da telemetria.
5. WHEN uma mensagem válida volta a chegar depois de um período `UNAVAILABLE` THEN a ponte SHALL reportar o novo valor com status `AVAILABLE`.
6. IF `speed` é `NaN`, infinito ou tem módulo acima da velocidade máxima do mapa (ex.: `80.1` no CAT793F, limite 80 km/h inclusivo) THEN a ponte SHALL reportar `PERF_VEHICLE_SPEED` com status `ERROR`.
7. IF `gear` da telemetria está fora de `-9..9`, ou `transmission` da alavanca do CAT793F está fora da tabela, THEN a ponte SHALL reportar a propriedade traduzida daquele campo (`CURRENT_GEAR`, e `GEAR_SELECTION` quando derivada dele, ou `GEAR_SELECTION` da alavanca) com status `UNAVAILABLE` e motivo `UNKNOWN_GEAR`.
8. IF chegam `SirvVehicleControl` com dois `id` diferentes dentro do limite de dado velho THEN a ponte SHALL reportar `PARKING_BRAKE_ON` com status `UNAVAILABLE` e motivo `AMBIGUOUS_SOURCE` até passar um limite de dado velho inteiro com um só `id`.
9. WHEN uma propriedade muda de status (`AVAILABLE`, `UNAVAILABLE`, `ERROR`) THEN a ponte SHALL registrar no logcat (tag `vipee_bridge`) a propriedade, o status novo e o motivo nomeado (`STALE`, `NO_DATA`, `OUT_OF_RANGE`, `UNKNOWN_GEAR`, `AMBIGUOUS_SOURCE`, `MAP_INVALID`).

**Independent Test**: No núcleo, com relógio fake: sem mensagens, com mensagem e avanço de 501 ms, com `NaN`, com `gear = 10`, com dois `id` alternando, com alavanca velha no mapa do CAT793F. No emulador: parar o publicador fake e ver as propriedades ficarem `UNAVAILABLE`.

---

### P1: Mapa por veículo, tabelado e versionado ⭐ MVP

**User Story**: Como mantenedor da ponte, quero que o que varia por veículo venha de um mapa versionado por veículo para trocar de veículo ou ajustar um limite sem mexer em código.

**Why P1**: Restrição herdada (AGENTS.md §1.1); CAT793F e Commander diferem em velocidade máxima e na fonte da alavanca.

**Acceptance Criteria**:

1. The ponte SHALL ler de um mapa versionado por veículo: versão do mapa, namespace do veículo, tópico e campo de origem de cada propriedade, escala de unidade, velocidade máxima, limite de dado velho e fonte de `GEAR_SELECTION` (alavanca com tabela, ou derivada da engatada).
2. WHEN a ponte inicia configurada para um veículo (ex.: `commander`) THEN a ponte SHALL carregar o mapa daquele veículo e assinar `/<namespace do mapa>/telemetry`.
3. The repositório SHALL trazer os mapas `cat793f` (alavanca `CAT793FVehicleControl.transmission`, máxima 80 km/h) e `commander` (seleção derivada da engatada, máxima 220 km/h), ambos com limite de 500 ms.
4. IF o mapa é ilegível, tem versão desconhecida, falta campo obrigatório ou referencia propriedade VHAL fora das 4 desta fatia THEN a ponte SHALL recusar o mapa, registrar `MAP_INVALID` no logcat e manter as 4 propriedades `UNAVAILABLE`.

**Independent Test**: No núcleo, carregar os dois mapas e um inválido; com a mesma telemetria (marcha engatada `0`), o CAT793F com alavanca `'P'` dá `GEAR_PARK` e o Commander dá `GEAR_NEUTRAL`.

---

## Edge Cases

- IF o processo ROS da ponte reinicia THEN a ponte SHALL voltar a reportar `UNAVAILABLE` até a primeira mensagem válida (CTS-11).
- IF o simulador pausa (`/pause_simulation`) e para de publicar THEN a ponte SHALL reportar `UNAVAILABLE` após o limite de dado velho (CTS-12).
- WHEN `speed = -0.0` THEN a ponte SHALL publicar `PERF_VEHICLE_SPEED = 0.0` (CTS-02).
- WHEN `speed = 80.0` no CAT793F THEN a ponte SHALL aceitar o valor (limite inclusivo) (CTS-16).
- WHEN `gear = -2` (segunda ré, se o veículo tiver) THEN a ponte SHALL publicar `CURRENT_GEAR = GEAR_REVERSE` (CTS-03).

---

## Implicit-requirement sweep

| Dimension | Resolution |
| --------- | ---------- |
| Input validation & bounds | CTS-16, CTS-17, CTS-18, CTS-23 |
| Failure / partial-failure states | CTS-11..CTS-15; tópicos independentes: falha de um não afeta as propriedades do outro, inclusive a alavanca do CAT793F (CTS-14) |
| Idempotency / retry / duplicate handling | CTS-08 (sem evento repetido em ON_CHANGE) |
| Auth boundaries & rate limits | N/A because o acesso às propriedades é o do VHAL (READ para apps com permissão do AAOS); a ponte não cria superfície nova. Domínio SELinux já restrito (AD-012) |
| Concurrency / ordering | Último valor vence por tópico, na ordem de chegada; callbacks ROS e leituras do VHAL concorrentes não podem ver valor e status de mensagens diferentes (Design) |
| Data lifecycle / expiry | CTS-12, CTS-13 (limite de dado velho do mapa) |
| Observability | CTS-19 (logcat com motivo nomeado) |
| External-dependency failure | CTS-12..CTS-14 (simulador parado ou link perdido); CTS-23 (mapa inválido) |
| State-transition integrity | CTS-11..CTS-15 (`UNAVAILABLE` ⇄ `AVAILABLE`, `ERROR`); CTS-04..CTS-06 (P só com fonte de alavanca) |

---

## Requirement Traceability

| Requirement ID | Story | Phase | Status |
| -------------- | ----- | ----- | ------ |
| CTS-01 | P1: Sinais no VHAL (AC1, velocidade em m/s) | Design | Pending |
| CTS-02 | P1: Sinais no VHAL (AC2, velocidade com sinal) | Design | Pending |
| CTS-03 | P1: Sinais no VHAL (AC3, `CURRENT_GEAR` genérica) | Design | Pending |
| CTS-04 | P1: Sinais no VHAL (AC4, `GEAR_SELECTION` da alavanca do CAT793F) | Design | Pending |
| CTS-05 | P1: Sinais no VHAL (AC5, `GEAR_SELECTION` derivada) | Design | Pending |
| CTS-06 | P1: Sinais no VHAL (AC6, sem P na seleção derivada) | Design | Pending |
| CTS-07 | P1: Sinais no VHAL (AC7, freio de mão) | Design | Pending |
| CTS-08 | P1: Sinais no VHAL (AC8, sem evento repetido) | Design | Pending |
| CTS-09 | P1: Sinais no VHAL (AC9, carimbo na chegada) | Design | Pending |
| CTS-10 | P1: Sinais no VHAL (AC10, latência ≤ 200 ms) | Design | Pending |
| CTS-11 | P1: Fail-loud (AC1, sem dado desde o início) | Design | Pending |
| CTS-12 | P1: Fail-loud (AC2, telemetria velha) | Design | Pending |
| CTS-13 | P1: Fail-loud (AC3, controle velho) | Design | Pending |
| CTS-14 | P1: Fail-loud (AC4, alavanca velha no CAT793F) | Design | Pending |
| CTS-15 | P1: Fail-loud (AC5, volta a AVAILABLE) | Design | Pending |
| CTS-16 | P1: Fail-loud (AC6, velocidade inválida) | Design | Pending |
| CTS-17 | P1: Fail-loud (AC7, marcha ou alavanca desconhecida) | Design | Pending |
| CTS-18 | P1: Fail-loud (AC8, `id` ambíguo) | Design | Pending |
| CTS-19 | P1: Fail-loud (AC9, log de transição) | Design | Pending |
| CTS-20 | P1: Mapa (AC1, campos do mapa) | Design | Pending |
| CTS-21 | P1: Mapa (AC2, mapa escolhido pelo veículo) | Design | Pending |
| CTS-22 | P1: Mapa (AC3, mapas CAT793F e Commander) | Design | Pending |
| CTS-23 | P1: Mapa (AC4, mapa inválido) | Design | Pending |

**Coverage:** 23 total, 0 mapped to tasks, 23 unmapped ⚠️

---

## Success Criteria

- [ ] Com o publicador fake rodando, as 4 propriedades lidas no emulador batem com o publicado em 10 leituras seguidas, cada uma em até 200 ms após a publicação.
- [ ] Parando o publicador, as 4 propriedades ficam `UNAVAILABLE` em até 600 ms e o logcat mostra `STALE`.
- [ ] Com o mapa do CAT793F a alavanca `'P'` dá `GEAR_PARK`; com o do Commander a mesma telemetria dá a seleção derivada, sem recompilar.
