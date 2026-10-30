# Contrato Simulador ➔ ROS2AAOSBridge

**Versão:** 0.2.1 (rascunho para discussão) · **Data:** 2026-10-30 · **Escopo:** MVP do demo no RPi5 (prazo 2026-11-28)

Este documento define o que o simulador publica em ROS2 para que a ponte entregue os sinais ao Android Automotive (VHAL e EVS) sem que o dashboard perceba que a fonte é simulada. A v0.2 parte do que o simulador **já publica**, levantado do repositório `SIRV-Simulator`, branch `SIRV_UE5.5_ROS2_Jazzy` (2026-10-30), em vez dos tópicos `/vehicle/...` propostos na v0.1. O que falta foi solicitado ao time do simulador e aparece como **pedido** (§4). As decisões do lado da ponte estão em `.specs/STATE.md` (AD-011 e AD-012: o processo ROS da ponte roda dentro do Android).

## 1. Princípios

1. **Semântica de veículo, não de Android.** O simulador publica "marcha em ré", "porta dianteira esquerda aberta". Ele não conhece ids do VHAL nem tipos do EVS. A tradução é da ponte, num mapa tabelado e versionado (tópico, campo, unidade, escala, taxa, política de dado velho).
2. **Unidades e frames declarados.** Cada campo usado tem unidade e frame escritos neste contrato. Onde o simulador usa unidade não SI (km/h) ou a convenção do Unreal, a conversão é da ponte e fica no mapa.
3. **Ausência é informação.** Sinal que o simulador não tem não é publicado. A ponte marca a propriedade como indisponível. Nunca publique valor padrão "para preencher": o dashboard confia na ponte como confiaria no veículo.
4. **Vivo ou velho.** Cada sinal tem taxa mínima. Passado o limite sem mensagem, a ponte declara o sinal indisponível e, nas câmeras, para o stream. Um valor congelado parece normal e é o pior caso.
5. **Mesmo instante, mesmo carimbo.** Mensagens que descrevem o mesmo instante da simulação levam o mesmo `header.stamp` (em especial as 4 câmeras de um mesmo quadro).

## 2. Convenções e transporte

* **ROS 2 Jazzy**, `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`, `ROS_DOMAIN_ID=25` (o simulador já usa os três; `sirv_simulator.sh`).
* **Pacote de mensagens:** `sirv_msgs`, do próprio simulador. A ponte compila o pacote para Android com o NDK, então mudança em `sirv_msgs` é mudança de contrato (§7).
* **Namespace do veículo:** tópicos do veículo ficam em `/<vehicle_namespace>/`, definido por veículo em `sirv_vehiclesim/params/vehicles/<veículo>.yaml`. No CAT793F é `cat793f`. A ponte recebe o namespace por configuração; nada de `/cat793f` fixo no código.
* **Descoberta (obrigatório para a ponte):** o Android não recebe multicast (NAT do emulador, Wi-Fi do RPi5). O CycloneDDS do host roda **só em TCP, sem multicast**. Cada processo ROS do simulador escuta numa porta TCP fixa, e os processos do host se conhecem por peers unicast. O processo da ponte no Android disca para todas essas portas (`<TCP><Port>-1</Port>`). No handoff, o UE5 escutava em 7500 e o `sirv_cat793fsim` em 7501 (`Refs/handoff-ros2-jazzy-aaos.md` §7.2). O `sirv_ros/config/cyclonedds.xml` versionado hoje é UDP e serve só para uso dentro do host; os XML TCP do handoff não estão no repositório do simulador (pedido P5).

## 3. Sinais que o simulador já publica

| Sinal do MVP | Tópico | Tipo e campo | Unidade | Taxa | QoS | Destino no AAOS |
| ------------ | ------ | ------------ | ------- | ---- | --- | --------------- |
| Velocidade | `/<ns>/telemetry` | `sirv_msgs/SirvVehicleTelemetry.speed` (float32) | km/h | a medir | reliable, depth 5 | `PERF_VEHICLE_SPEED` (m/s) |
| Marcha | `/<ns>/telemetry` | `SirvVehicleTelemetry.gear` (int8) | código (§3.2) | a medir | reliable, depth 5 | `GEAR_SELECTION`, `CURRENT_GEAR` |
| Alavanca (CAT793F) | `/<ns>/vehicle_control` | `sirv_msgs/CAT793FVehicleControl.transmission` (int8) | ASCII `P R N D 1 2` | a cada leitura do cockpit | reliable, depth 1 | `GEAR_SELECTION` |
| Freio de mão | `/apply_control_to_vehicle` | `sirv_msgs/SirvVehicleControl.handbrake` (bool) | — | a medir | reliable, depth 2 | `PARKING_BRAKE_ON` |
| Câmeras (×4) | `/sirv/sensor/fisheye/<pos>/image/compressed` | `sensor_msgs/CompressedImage` | — | 10 Hz (`sensor_tick: 0.1`) | reliable, volatile, depth 1 | EVS (§3.3) |
| Intrínsecos (×4) | `/sirv/sensor/fisheye/<pos>/image/camera_info` | `sensor_msgs/CameraInfo` | px | junto da imagem | reliable, volatile, depth 1 | calibração (CalibDroid) |

"A medir": o simulador publica telemetria a cada passo de `run_simulation` (timer de 15 ms) depois de uma chamada síncrona ao serviço `/get_vehicle_telemetry` do UE5, então a taxa real depende do UE5. Medimos com `ros2 topic hz` no primeiro teste integrado e fixamos aqui o mínimo aceito.

### 3.1 Velocidade

`SirvVehicleTelemetry.speed` em km/h **com sinal**: o UE5 preenche `GetForwardSpeed() * 0.036` do Chaos (`SirvWheeledVehicle.cpp:258`), positivo para frente e negativo andando para trás, independente da marcha. É a mesma convenção do `PERF_VEHICLE_SPEED`, então a ponte só converte para m/s (`÷ 3,6`).

### 3.2 Marcha

`SirvVehicleTelemetry.gear` é a marcha **engatada** da transmissão Chaos (`GetCurrentGear()`, `SirvWheeledVehicle.cpp:256`), com o mesmo esquema para todos os veículos: `< 0` ré, `0` neutro, `1..n` marcha à frente. A física não tem P. A alavanca do operador vem por outro tópico e varia por veículo:

* **CAT793F:** `/cat793f/vehicle_control`, `CAT793FVehicleControl.transmission`, publicado pelo `cat793f_analogic_bridge` a partir do cockpit. Códigos em ASCII: `'P'` (80), `'R'` (82), `'N'` (78), `'D'` (68, marchas 3 a 6), `'1'` (49), `'2'` (50); código desconhecido é tratado como P pelo simulador. Em P o simulador também puxa o freio de mão (`cat793f_driving_logic.cpp:739-782`), característica do CAT793F.
* **Veículos genéricos (Commander etc.):** o `generic_logic` recebe em `/<ns>/vehicle_control` só a marcha numérica (`SirvVehicleControl.gear`), sem P nem D. Todo automático tem P na alavanca, então isso fica no pedido P7. Freio de mão e marcha são independentes (D com freio de mão puxado é possível).

Na ponte: `CURRENT_GEAR` vem da engatada. `GEAR_SELECTION` vem da alavanca no CAT793F; nos genéricos é derivada da engatada (ré, neutro ou `GEAR_DRIVE`), sem P até o P7. Código de alavanca desconhecido vira `UNAVAILABLE` na ponte, em vez de P.

### 3.3 Freio de mão

`/apply_control_to_vehicle` está fora do namespace do veículo e é o **comando** enviado ao UE5 (campo `id` identifica o veículo), não o estado lido do veículo. Serve para o MVP: a ponte filtra pelo `id` configurado. O `CarDrivingStateService` do AAOS exige `PARKING_BRAKE_ON` junto de `PERF_VEHICLE_SPEED` e `GEAR_SELECTION`. Um estado de freio de mão na telemetria seria o certo a médio prazo (pedido P3).

### 3.4 Câmeras

O rig fisheye 360° do CAT793F (`params/vehicles/CAT793F.yaml`, bloco `fisheye`):

| Posição (`<pos>`) | EVS | `role_name` | Pose `[x, y, z, roll, pitch, yaw]` |
| ----------------- | --- | ----------- | ---------------------------------- |
| `front` | `FRONTVIEW` | `FisheyeFront` | `[6.42, -1.19, 3.80, 0, -0.0873, 0]` |
| `rear` | `REARVIEW` | `FisheyeRear` | `[-7.0, 0, 3.80, 0, -0.5236, 3.1416]` |
| `left` | `LEFTVIEW` | `FisheyeLeft` | `[0, -3.5, 3.80, 0, -0.5236, -1.5708]` |
| `right` | `RIGHTVIEW` | `FisheyeRight` | `[0, 3.5, 3.80, 0, -0.5236, 1.5708]` |

* **Resolução 480×480**, FOV 180° × 180°, modelo Kannala-Brandt. O `camera_info` traz `distortion_model: equidistant` e `D = [k0, k1, k2, k3]`, que o `cv::fisheye` usa direto. Os coeficientes são sintéticos, não de lente real. O MVP pedia 720p; ver §6.
* Cada câmera publica também `<topic>` (`sensor_msgs/Image` cru) e `<topic>/compressed`. **A ponte assina só o `/compressed`**: o cru das 4 câmeras não cabe na rede até o RPi5. O `/compressed` é JPEG (TurboJPEG, qualidade 80).
* **Poses no frame do Unreal**, em m e rad, relativas à origem do ator do veículo: X à frente, **Y à direita** (a câmera esquerda está em `y = -3.5`), Z para cima, yaw positivo girando para a direita e pitch negativo olhando para baixo. Isso difere do frame do veículo da ponte (origem no solo sob o eixo dianteiro, X à frente, Y à esquerda, Z para cima; CalibDroid AD-004). A ponte converte com `y ➔ -y`, `yaw ➔ -yaw` e a translação até o eixo dianteiro. O `Commander.yaml` documenta a convenção (as poses vão sem mudança, de m para cm e de rad para graus, para o frame local do ator no UE; GNSS e IMU são publicados em REP-103) e a origem da malha do Commander: no solo, no meio do entre-eixos (eixo traseiro em `x = -1.24`). **A confirmar:** a origem do ator do CAT793F e a ordem de rotação.
* As poses de `left`, `right` e `rear` são estimativas iniciais (só a `front` está calibrada na cena). Para o demo isso basta; para calibração, o CalibDroid estima as extrínsecas.
* **GPU:** o próprio YAML avisa que as 4 câmeras fisheye ligadas ao mesmo tempo causaram `VK_ERROR_DEVICE_LOST` na máquina do simulador. O demo depende de as 4 rodarem juntas (pedido P4).

## 4. Pedidos ao simulador

Solicitados ao time do simulador em 2026-10-30. O formato abaixo é a proposta da ponte; nome e tipo finais são do simulador, desde que a semântica se mantenha.

| Id | Pedido | Proposta | Destino no AAOS |
| -- | ------ | -------- | --------------- |
| P1 | Estado das portas | `/<ns>/doors`, mensagem com abertura por porta (0,0 fechada a 1,0 aberta; `NaN` = porta inexistente), lado no frame do veículo, publicado na mudança + 1 Hz, reliable + transient_local | `DOOR_POS` |
| P2 | Estado dos retrovisores | `/<ns>/mirrors`, `bool left_folded`, `bool right_folded`, na mudança + 1 Hz, reliable + transient_local | `MIRROR_FOLD` (ver §6) |
| P3 | Freio de mão como estado | campo na telemetria ou tópico próprio em `/<ns>/` | `PARKING_BRAKE_ON` |
| P4 | 4 câmeras fisheye simultâneas | as 4 ligadas no demo, com o mesmo `header.stamp` por quadro | EVS |
| P5 | Config CycloneDDS TCP versionada | os XML de host do handoff (UE5 em 7500, `sirv_vehiclesim` em 7501) no repositório do simulador, com as portas fixas | descoberta (§2) |
| P6 | Dados do veículo publicados | `/<ns>/vehicle_info`, transient_local: velocidade máxima, número de marchas, tipo de transmissão, se o freio de mão é ligado à alavanca em P. Até lá, esses dados ficam no mapa por veículo da ponte | validação e regra de P |
| P7 | Alavanca de câmbio nos veículos genéricos | posição da alavanca, incluindo P (`P`, `R`, `N`, `D` e, no modo manual, a marcha), em `/<ns>/`, na mudança + 1 Hz. O CAT793F já publica (`CAT793FVehicleControl.transmission`); o `generic_logic` só recebe a marcha numérica | `GEAR_SELECTION` |

"Na mudança + 1 Hz": publica imediatamente quando muda e repete o último valor a cada segundo como heartbeat. `transient_local` entrega o último valor a quem conecta depois (a ponte pode reiniciar com o simulador rodando).

## 5. Tempo

A ponte carimba os dados com o relógio do Android na chegada (AD-010), como faria a HAL de um veículo real. O carimbo do simulador segue na mensagem e serve para medir latência e agrupar as câmeras. O `sirv_ros` publica `/clock`.

Perguntas ao simulador:

1. Roda em tempo real (fator 1,0)? Pode desacelerar sob carga?
2. Os nós do simulador usam `use_sim_time`?
3. Pausa ou executa passo a passo durante o demo (`/pause_simulation`)?

Se o tempo do simulador não for real, a latência medida pela ponte perde sentido, mas a injeção continua correta.

## 6. Pontos em aberto

* **720p ou 480×480.** O MVP pedia 4 câmeras 720p para o CalibDroid. O simulador entrega 480×480 a 10 Hz, e subir a resolução pesa na GPU (§3.4). Para o demo, 480×480 serve; a calibração em 720p é decisão do stakeholder.
* **QoS reliable nas câmeras.** As câmeras publicam com `reliable`. A ponte assina com `best_effort` (compatível), mas um link lento até o Android ainda faz o publicador reter amostras. Se a latência subir no RPi5, pedir `best_effort` no publicador.
* **Retrovisor por lado.** A propriedade padrão `MIRROR_FOLD` do AAOS 16 é global: um booleano para todos os retrovisores. Se o dashboard precisa cortar o BEV de um lado só, a ponte expõe também uma propriedade vendor por lado. O simulador publica por lado de qualquer jeito (P2).
* **Telemetria de pose.** `SirvVehicleTelemetry.position`/`orientation` vêm como `Vector3` sem frame nem unidade declarados. Fora do MVP.

## 7. Mudanças no contrato

Mudança de nome, tipo ou semântica sobe a versão deste documento, e mudança em `sirv_msgs` precisa ser recompilada na ponte para Android. As duas precisam do acordo dos dois lados antes de serem publicadas. Sinal novo entra por adição, sem quebrar os existentes.

### Histórico

* **0.2.1 (2026-10-30):** velocidade com sinal e marcha engatada genérica lidas do código do simulador (perguntas respondidas); alavanca do CAT793F em `/<ns>/vehicle_control`; origem do Commander; pedidos P6 e P7.
* **0.2 (2026-10-30):** parte dos tópicos e de `sirv_msgs` do simulador (branch `SIRV_UE5.5_ROS2_Jazzy`). Abandona `/vehicle/...` e `vipee_vehicle_msgs`. Jazzy + CycloneDDS TCP. Portas, retrovisores e freio de mão como estado viram pedidos.
* **0.1 (2026-10-28):** proposta inicial, com tópicos e mensagens próprios.
