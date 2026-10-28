# VIPEE-ROS2AAOSBridge — Project State

## Decisions

| ID | Date | Decision | Rationale |
| -- | ---- | -------- | --------- |
| AD-001 | 2026-10-28 | Dois canais host ➔ AAOS: controle e telemetria (C&T) separados do vídeo | Vídeo grande não pode atrasar telemetria (head-of-line do TCP); o dashboard exige baixa latência nos sinais de veículo |
| AD-002 | 2026-10-28 | Canal C&T: WebSocket com protobuf (contrato `.proto` versionado) | Equipe já tem experiência com WebSocket; protobuf dá contrato tipado para C++, Kotlin e Python. Mapa tópico ROS2 ➔ propriedade VHAL fica tabelado (AGENTS.md §1.1) |
| AD-003 | 2026-10-28 | Canal de vídeo: WebSocket binário, atrás de interface própria (`FrameSink`/`FrameSource`). Migrar para UDP/RTP ou outro só se a medição mostrar head-of-line ou latência ruim | Começa pelo mais simples; a interface própria mantém a migração barata (política de adapter herdada do SynthCamStream). Critérios de migração viram requisito mensurável no Specify |
| AD-004 | 2026-10-28 | O CalibDroid recebe as 4 câmeras por esta ponte (FRONTVIEW/REARVIEW/LEFTVIEW/RIGHTVIEW, CalibDroid AD-027). Fonte Unreal a 720p (1280×720); o bag do CalibDroid é a referência de regressão | Cumpre CalibDroid AD-016. Orçamento a 30 fps (assumido; confirmar): cinza cru ≈ 27,6 MB/s por câmera, ≈ 885 Mbps nas 4; RGB cru ≈ 2,65 Gbps. Cru só cabe em loopback/emulador; GbE do RPi5 exige JPEG no vídeo normal. O formato é configurável por câmera |
| AD-005 | 2026-10-28 | AOSP-alvo: Android 16 nas três etapas. No RPi5, produto `aosp_rpi5_car` do raspberry-vanilla (`lunch aosp_rpi5_car-bp2a-userdebug`, base `android-16.0.0_r1`); no emulador, a árvore local `~/aosp` (`android-16.0.0_r4`) | A API do VHAL e do EVS muda entre releases; mesma versão do CalibDroid evita divergência. Artigo em `Refs/android-auto-RPi5.pdf` (N. Harny, 2025-09-07) mostra o launcher automotivo rodando no RPi5. **Não verificado:** qual VHAL e se há EVS nessa imagem (o artigo não fala), e a diferença r1 ➔ r4 nas HALs. Verificar na primeira semana |
| AD-006 | 2026-10-28 | Nó ROS2 do host em C++17 com `rclcpp` | Desempenho com 4 vídeos 720p (evita cópias e GIL do Python) e uso dos recursos do C++17; alinhado ao núcleo do CalibDroid |
| AD-007 | 2026-10-28 | Sinais do MVP: marcha, velocidade, porta aberta, retrovisor dobrado. Os demais entram por feature, pelo mapa tabelado | Marcha valida a câmera de ré contra a legislação; velocidade decide a solicitação de câmera; porta aberta e retrovisor dobrado cortam o sinal do BEV. Candidatas no `VehicleProperty.aidl` do AOSP 16: `GEAR_SELECTION`/`CURRENT_GEAR`, `PERF_VEHICLE_SPEED`, `DOOR_POS` (por porta), `MIRROR_FOLD`; confirmar semântica e área de cada uma no Specify |
| AD-008 | 2026-10-28 | No Android, a ponte vive em dois processos HAL: o VHAL dono do canal C&T e a HAL EVS dona do canal de vídeo. O cliente WebSocket, o protobuf e a reconexão ficam numa biblioteca Soong compartilhada | Acima da fronteira HAL o Android vê um VHAL e um EVS comuns (transparência de origem); no veículo troca-se só o backend. Sem daemon extra. Rejeitados: daemon de ponte com HALs finas (IPC a mais, componente que não existe no veículo) e app/serviço Java (o `CarEvsManager` só consome; expõe a simulação aos apps) |
| AD-009 | 2026-10-28 | VHAL: `DefaultVehicleHal` do AOSP com `IVehicleHardware` próprio cujo backend é o canal C&T. Só as propriedades alimentadas pelo simulador são declaradas; propriedade sem dado ou velha fica `UNAVAILABLE` | A `fake_impl` do AOSP é só para teste e declara propriedades com valores iniciais plausíveis (plausível-mas-errado). O VHAL gRPC (`GRPCVehicleHardware`, Cuttlefish) fica como plano B se o prazo apertar, exigindo nova AD sobre a AD-002 |
| AD-010 | 2026-10-28 | Carimbo de tempo na chegada, com `elapsedRealtimeNano()` do Android, para VHAL e EVS. O protobuf carrega também o carimbo do simulador, número de sequência e, no vídeo, o identificador de grupo do conjunto de 4 câmeras. Heartbeat no C&T; perda de link ou `max_age` excedido torna as propriedades `UNAVAILABLE` e para o vídeo | Igual à HAL de um veículo real e tolerante a pausa e fator de tempo ≠ 1 do simulador. Os campos extras permitem medir latência e migrar para conversão por offset (sincronia exata das câmeras) sem mudar o contrato |

## Handoff

### Pivô em avaliação: ROS2 dentro do AAOS (2026-10-28)

- O colega do simulador relata ter compilado e executado ROS2 como módulo no AAOS e vai gerar um handoff. Se confirmado, o nó do host e os canais WebSocket (AD-001 a AD-003, AD-006) são substituídos por assinatura direta nas HALs; AD-008 a AD-010 continuam com outro backend. Contraria a premissa herdada do CalibDroid AD-011, então só vira AD depois de reproduzido aqui.
- Specify da `ct-speed-slice` pausado no meio da discussão. Achados que valem para qualquer arquitetura: `CarDrivingStateService` exige `PERF_VEHICLE_SPEED`, `GEAR_SELECTION` e `PARKING_BRAKE_ON` (falta freio de mão no contrato); o `CarService` precisa de propriedades de plataforma sem fonte no simulador (`AP_POWER_STATE_REQ`/`REPORT`).

### Marco: demo do simulador acoplado ao dashboard no RPi5 (prazo: 2026-11-28)

- Pedido do stakeholder. Contrato com o simulador em rascunho (`docs/sim-contract.md` v0.1), aguardando o colega do simulador. Próximo passo: Specify da feature de MVP.
- Risco principal: a mula RPi5 com AAOS e a HAL EVS virtual nunca rodaram juntas; validar cedo, não na última semana.

### Setup (2026-10-28)

- Repo inicializado, ferramental copiado do VIPEE-CalibDroid. Nenhuma feature especificada ainda; próximo passo: Specify da primeira feature (ver AGENTS.md §1 para as restrições herdadas).
