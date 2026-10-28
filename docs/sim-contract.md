# Contrato Simulador ➔ ROS2AAOSBridge

**Versão:** 0.1 (rascunho para discussão) · **Data:** 2026-10-28 · **Escopo:** MVP do demo no RPi5 (prazo 2026-11-28)

Este documento define o que o simulador publica em ROS2 para que a ponte entregue os sinais ao Android Automotive (VHAL e EVS) sem que o dashboard perceba que a fonte é simulada. Ele é proposta: nomes, tipos e taxas estão abertos para negociação com o time do simulador. As decisões já fechadas do lado da ponte estão em `.specs/STATE.md` (AD-001 a AD-010).

## 1. Princípios

1. **Semântica de veículo, não de Android.** O simulador publica "marcha em ré", "porta dianteira esquerda 30% aberta". Ele não conhece ids do VHAL nem tipos do EVS. A tradução é da ponte, num mapa tabelado e versionado.
2. **Unidades SI e frames nomeados.** Metros, m/s, radianos, segundos. Toda mensagem com `header.frame_id` preenchido.
3. **Ausência é informação.** Sinal que o simulador não tem não é publicado (ou vai como `NaN` onde indicado). A ponte marca a propriedade como indisponível. Nunca publique valor padrão "para preencher": o dashboard confia na ponte como confiaria no veículo.
4. **Vivo ou velho.** Cada sinal tem taxa mínima. Passado o limite sem mensagem, a ponte declara o sinal indisponível e, nas câmeras, para o stream. Um valor congelado parece normal e é o pior caso.
5. **Mesmo instante, mesmo carimbo.** Mensagens que descrevem o mesmo instante da simulação levam o mesmo `header.stamp` (em especial as 4 câmeras de um mesmo quadro).

## 2. Convenções

* **Domínio ROS2:** `ROS_DOMAIN_ID=25`, ROS2 Humble.
* **Namespace:** `/vehicle/...` para estado do veículo, `/vehicle/camera/<posição>/...` para câmeras.
* **Frame do veículo (`frame_id: vehicle`):** origem na projeção no solo da interseção do eixo dianteiro com a linha central; X à frente, Y à esquerda, Z para cima. A orientação coincide com o `base_link` da REP-103; a origem não (o `base_link` costuma ficar no eixo traseiro). Se o simulador já tem `base_link`, publique o TF `vehicle ➔ base_link` estático.
* **Pacote de mensagens:** `vipee_vehicle_msgs` (proposta), versionado num repositório compartilhado entre simulador e ponte. Usamos mensagens padrão do ROS2 onde elas servem e mensagens próprias só onde não há equivalente.

## 3. Sinais do MVP

| Sinal | Tópico | Tipo | Taxa | QoS | Destino no AAOS |
| ----- | ------ | ---- | ---- | --- | --------------- |
| Marcha | `/vehicle/gear` | `vipee_vehicle_msgs/msg/Gear` | na mudança + 1 Hz | reliable, transient_local, depth 1 | `GEAR_SELECTION`, `CURRENT_GEAR` |
| Velocidade | `/vehicle/twist` | `geometry_msgs/msg/TwistStamped` | ≥ 20 Hz | best_effort, depth 1 | `PERF_VEHICLE_SPEED` |
| Portas | `/vehicle/doors` | `vipee_vehicle_msgs/msg/Doors` | na mudança + 1 Hz | reliable, transient_local, depth 1 | `DOOR_POS` (por porta) |
| Retrovisores | `/vehicle/mirrors` | `vipee_vehicle_msgs/msg/Mirrors` | na mudança + 1 Hz | reliable, transient_local, depth 1 | `MIRROR_FOLD` (ver §6) |
| Câmeras (×4) | `/vehicle/camera/<pos>/image_raw` | `sensor_msgs/msg/Image` | 30 Hz | best_effort, depth 1 | EVS (`FRONTVIEW`, `REARVIEW`, `LEFTVIEW`, `RIGHTVIEW`) |
| Intrínsecos (×4) | `/vehicle/camera/<pos>/camera_info` | `sensor_msgs/msg/CameraInfo` | junto da imagem | best_effort, depth 1 | calibração (CalibDroid) |
| Relógio | `/clock` | `rosgraph_msgs/msg/Clock` | ver §5 | padrão | diagnóstico de latência |

"Na mudança + 1 Hz": publica imediatamente quando muda e repete o último valor a cada segundo como heartbeat. `transient_local` entrega o último valor a quem conecta depois (a ponte pode reiniciar com o simulador rodando).

### 3.1 Marcha (`Gear.msg`)

```
std_msgs/Header header
uint8 UNKNOWN=0
uint8 PARK=1
uint8 REVERSE=2
uint8 NEUTRAL=3
uint8 DRIVE=4
uint8 GEAR_1=11      # 11..19 = marchas 1 a 9 (manual ou modo manual do automático)
uint8 selected       # posição da alavanca escolhida pelo motorista
uint8 engaged        # marcha que a transmissão está usando de fato
```

Por quê as duas: a câmera de ré obrigatória por legislação reage à seleção de ré; `engaged` alimenta `CURRENT_GEAR`. `UNKNOWN` vira indisponível na ponte.

### 3.2 Velocidade (`TwistStamped`)

`frame_id: vehicle`. Só `twist.linear.x` é usado no MVP: velocidade longitudinal em m/s, **positiva para frente e negativa em marcha a ré**, independente da marcha (mesma convenção do `PERF_VEHICLE_SPEED`). Os demais campos podem vir preenchidos (taxa de guinada será útil depois); campo não simulado vai como `NaN`.

### 3.3 Portas (`Doors.msg`)

```
std_msgs/Header header
# Abertura de 0.0 (fechada) a 1.0 (totalmente aberta). NaN = porta inexistente ou estado desconhecido.
float32 front_left
float32 front_right
float32 rear_left
float32 rear_right
float32 hood
float32 trunk
```

Esquerda e direita no frame do veículo (Y positivo à esquerda), não do ponto de vista de quem olha o carro.

### 3.4 Retrovisores (`Mirrors.msg`)

```
std_msgs/Header header
bool left_folded
bool right_folded
```

### 3.5 Câmeras

| Posição (`<pos>`) | EVS | `frame_id` |
| ----------------- | --- | ---------- |
| `front` | `FRONTVIEW` | `camera_front` |
| `rear` | `REARVIEW` | `camera_rear` |
| `left` | `LEFTVIEW` | `camera_left` |
| `right` | `RIGHTVIEW` | `camera_right` |

* 1280×720, `encoding` `mono8` ou `bgr8` (a confirmar qual o simulador gera mais barato), sem compressão. A ponte comprime quando precisa (JPEG no GbE do RPi5) e mantém o cru para a calibração.
* As 4 imagens de um mesmo quadro levam o **mesmo** `header.stamp`. A ponte usa isso para agrupar o conjunto.
* `camera_info` com o modelo e os intrínsecos teóricos da câmera virtual (o CalibDroid usa Kannala-Brandt; ver `../VIPEE-CalibDroid`, AD-003 e AD-005).
* Os nomes são mapeáveis por configuração da ponte: o `VIPEE-SynthCamStream` usa `/synthcam/{fv,rv,mvl,mvr}/image_raw` e continua servindo de fonte de teste.

## 4. Volume e transporte entre simulador e ponte

4 câmeras 720p a 30 Hz somam ~110 MB/s em `mono8` e ~330 MB/s em `bgr8`. Isso passa por DDS dentro da mesma máquina, mas não por rede. **Proposta:** simulador e nó da ponte rodam no mesmo host, e confirmamos cedo que o transporte por memória compartilhada do DDS está ativo (no Humble, o padrão é o Fast DDS, que usa memória compartilhada entre processos locais). Rodar em máquinas separadas exige rever este item.

## 5. Tempo

A ponte carimba os dados com o relógio do Android na chegada (AD-010), como faria a HAL de um veículo real. O carimbo do simulador segue na mensagem e serve para medir latência e agrupar as câmeras.

Perguntas ao simulador:

1. Roda em tempo real (fator 1,0)? Pode desacelerar sob carga?
2. Publica `/clock` e usa `use_sim_time`?
3. Pausa ou executa passo a passo durante o demo?

Se o tempo do simulador não for real, a latência medida pela ponte perde sentido, mas a injeção continua correta.

## 6. Pontos em aberto

* **Retrovisor por lado.** A propriedade padrão `MIRROR_FOLD` do AAOS 16 é global: um booleano para todos os retrovisores. Se o dashboard precisa cortar o BEV de um lado só, a ponte expõe também uma propriedade vendor por lado. O simulador publica por lado de qualquer jeito.
* **`mono8` ou `bgr8`.** Depende do custo no simulador e do que o dashboard mostra (o CalibDroid converte para cinza).
* **Heartbeat dedicado.** Hoje o heartbeat é a repetição de 1 Hz de cada sinal de estado. Um tópico `/vehicle/sim_status` com estado do simulador (rodando, pausado, cena) evitaria inferência; fica para depois do MVP se não for necessário.
* **Repositório do `vipee_vehicle_msgs`.** Onde vive e quem aprova mudanças.

## 7. Mudanças no contrato

Mudança de nome, tipo ou semântica sobe a versão deste documento e do pacote `vipee_vehicle_msgs`, e precisa do acordo dos dois lados antes de ser publicada. Sinal novo entra por adição, sem quebrar os existentes.
