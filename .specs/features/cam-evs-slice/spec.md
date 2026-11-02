# cam-evs-slice Specification

## Problem Statement

O CalibDroid (AD-016, AD-027) e o runtime AVM consomem as 4 câmeras fisheye pelo EVS, mas hoje o EVS do emulador toca MP4 fixos (`evs-hal-cf`). Para rodar acoplado ao simulador, as imagens publicadas em ROS2 precisam chegar ao EVS como câmeras comuns, sem que os consumidores saibam que a fonte é simulada. A primeira fonte é o bag de calibração do CalibDroid (`rosbag2_calibration_2026_10_01`), que já usa os tópicos, o formato e a resolução que o simulador usará.

## Goals

- [ ] Tocando o bag de calibração no host, um cliente `CarEvsManager` no emulador recebe as 4 câmeras (`FRONTVIEW`, `REARVIEW`, `LEFTVIEW`, `RIGHTVIEW`) em 1280×720, cada uma a pelo menos 8 quadros/s.
- [ ] Câmera sem dado novo para de entregar quadros; o EVS nunca repete um quadro velho como se fosse novo.
- [ ] Tópico, resolução e limite de dado velho de cada câmera vêm do mapa do veículo; trocar de veículo não muda código.
- [ ] A telemetria da `ct-speed-slice` mantém seus critérios com as 4 câmeras ligadas.

## Out of Scope

| Feature | Reason |
| ------- | ------ |
| SynthCamStream como fonte | Nomes (`/synthcam/<pos>/image_raw`), `rgb8` 1280×966 e sem `/compressed`; quando precisar do WoodScape, republicar com `image_transport republish` no host, sem mudar o SynthCamStream nem a ponte |
| `camera_info` e intrínsecos | O CalibDroid lê os intrínsecos do seu próprio arquivo; a ponte só leva pixels |
| Tópico `Image` cru | O cru das 4 câmeras não cabe na rede até o RPi5 (contrato §3.4); a ponte assina só o `/compressed` |
| Sincronia exata das 4 câmeras (grupo por `header.stamp`) | A JNI do `CarEvsService` descarta o carimbo de captura (CalibDroid AD-027); o consumidor agrupa pela chegada |
| `camera3P` e outras câmeras do bag | Não são câmeras do veículo |
| HAL EVS própria | Substituída por `v4l2loopback` + HAL `evs-v4l2` do AOSP (spike de 2026-10-30, STATE; AD-015). Exceção (AD-016): fork mínimo da `evs-v4l2` com 4 patches de robustez, sem HAL escrita do zero |
| Build arm64 e validação no RPi5 | Etapa seguinte; esta fatia fecha no emulador. `v4l2loopback` no kernel do RPi5 ainda não verificado |
| Ajuste do simulador para 1280×720 | Trabalho do time do simulador; a ponte rejeita outro tamanho até lá |
| Escrita do Android para o simulador | MVP é só leitura |

---

## Assumptions & Open Questions

| Assumption / decision | Chosen default | Rationale | Confirmed? |
| --------------------- | -------------- | --------- | ---------- |
| Fonte de teste | Bag de calibração do CalibDroid tocado no host (`ros2 bag play --loop`), um único participante DDS | Decisão do usuário (2026-11-02). Usa os tópicos `/sirv/sensor/fisheye/<pos>/image/compressed` do simulador; o host só aceita um participante na 7400 (AD-011) | y |
| Resolução de referência | 1280×720 nas 4 câmeras; o simulador será ajustado (hoje `Commander.yaml` publica 480×480) | Decisão do usuário (2026-11-02); AD-004 (720p) | y |
| Formato da imagem | `/compressed` em JPEG (`format` contendo `jpeg`); outro formato é rejeitado | Contrato v0.2 §3.4 (TurboJPEG, qualidade 80); o bag segue o mesmo | y |
| Mapeamento posição ➔ EVS | `front` ➔ `FRONTVIEW`, `rear` ➔ `REARVIEW`, `left` ➔ `LEFTVIEW`, `right` ➔ `RIGHTVIEW`, tabelado no mapa do veículo | Contrato §3.4; CalibDroid AD-027 | y |
| Veículo do bag | Commander (`/commander/telemetry` no bag); com `vendor.vipee.vehicle=commander` o mesmo bag alimenta VHAL e EVS | Lido do `metadata.yaml` do bag | y |
| Taxa da fonte | Bag a ~8,9 Hz por câmera (216 quadros em 24,3 s); simulador a 10 Hz | `metadata.yaml`; `sensor_tick: 0.1` | y |
| Limite de dado velho da câmera | 500 ms por câmera, configurável no mapa | ~4 períodos a 8,9 Hz; o mesmo limite padrão da telemetria | n |
| Latência por quadro | ≤ 100 ms do recebimento do quadro pelo processo ROS até o quadro estar disponível no dispositivo de vídeo, medida pelo processo ROS no emulador | Provisório; o caminho completo até o app passa pelo `evsmanagerd` e não tem carimbo confiável (AD-027) | n |
| FOV | Fora da ponte: CAT793F indefinido; Commander ~192° (o `Commander.yaml` diz 190°) | Informação do usuário (2026-11-02); vai para o contrato §3.4 | y |
| Troca da HAL EVS no emulador | A instalação troca a `evs-hal-cf` (MP4) pela `evs-v4l2` e é reversível, como o VHAL da `ct-speed-slice` | AVM e CalibDroid usam os MP4 hoje; o emulador é compartilhado (SHARED.md) | y |
| Build AOSP | A `evs-v4l2` e a política SELinux exigem build na VM `vipee` (`vm_m.sh`), com aprovação explícita antes | AGENTS.md §5; SHARED.md | y |

**Open questions:** none - os itens `n` acima são defaults de valor, ajustáveis no mapa.

---

## User Stories

### P1: As 4 câmeras do bag no EVS ⭐ MVP

**User Story**: Como desenvolvedor do CalibDroid e do AVM, quero receber pelo `CarEvsManager` as 4 câmeras publicadas em ROS2 para calibrar e montar a vista de cima com imagens do simulador.

**Why P1**: É o caminho de vídeo do demo; sem ele nenhum consumidor de câmera roda acoplado ao simulador.

**Acceptance Criteria**:

1. WHEN um quadro JPEG 1280×720 chega em `/sirv/sensor/fisheye/<pos>/image/compressed` THEN a ponte SHALL disponibilizá-lo à câmera EVS da posição mapeada (`FRONTVIEW`, `REARVIEW`, `LEFTVIEW` ou `RIGHTVIEW`) em 1280×720.
2. WHILE o bag toca, um cliente `CarEvsManager` com as 4 câmeras abertas SHALL receber de cada uma pelo menos 8 quadros por segundo.
3. The ponte SHALL disponibilizar cada quadro no dispositivo de vídeo em até 100 ms após recebê-lo, medido no processo ROS.
4. The ponte SHALL entregar cada quadro recebido no máximo uma vez, sem repetir quadro anterior quando não chega quadro novo.
5. WHILE as 4 câmeras estão ligadas, os critérios de latência (≤ 200 ms) e de dado velho (≤ 600 ms) da `ct-speed-slice` SHALL continuar valendo.

**Independent Test**: Tocar o bag no host com `vendor.vipee.vehicle=commander`; um cliente EVS no emulador conta quadros e confere o tamanho das 4 câmeras por 10 s.

---

### P1: Câmera sem dado não congela ⭐ MVP

**User Story**: Como desenvolvedor do AVM, quero que uma câmera sem fonte pare de entregar quadros para que a vista de cima nunca mostre uma imagem congelada como se fosse atual.

**Why P1**: Quadro congelado parece normal e é o pior caso (contrato, princípio 4; AGENTS.md §3, fail-loud).

**Acceptance Criteria**:

1. IF uma câmera fica mais que o limite de dado velho do mapa (padrão 500 ms) sem quadro válido THEN a ponte SHALL parar de entregar quadros a essa câmera e registrar `CAM <pos> STALE` no logcat.
2. WHEN chega um quadro válido a uma câmera em `STALE` THEN a ponte SHALL voltar a entregá-lo e registrar `CAM <pos> LIVE`, sem o cliente reabrir a câmera.
3. IF uma câmera para THEN as outras câmeras e as propriedades do VHAL SHALL seguir sem interrupção.

**Independent Test**: Com o bag tocando, parar o bag; em até 600 ms nenhum cliente recebe quadro novo e o logcat mostra `STALE` das 4; voltar a tocar e os quadros retornam.

---

### P1: Quadro inválido é descartado com motivo ⭐ MVP

**User Story**: Como integrador, quero que a ponte rejeite quadros que não pode entregar fielmente, com motivo nomeado, para achar a causa sem adivinhar.

**Why P1**: Fail-loud; um quadro redimensionado ou mal decodificado seria plausível-mas-errado para a calibração.

**Acceptance Criteria**:

1. IF o quadro decodificado não tem a largura e a altura do mapa THEN a ponte SHALL descartá-lo, sem redimensionar, com motivo `FRAME_SIZE_MISMATCH`.
2. IF o `format` do `CompressedImage` não é JPEG THEN a ponte SHALL descartar o quadro com motivo `FORMAT_UNSUPPORTED`.
3. IF a decodificação do JPEG falha THEN a ponte SHALL descartar o quadro com motivo `DECODE_ERROR`.
4. WHEN o motivo de descarte de uma câmera muda THEN a ponte SHALL registrar `CAM <pos> DROP <motivo>` no logcat uma vez, não a cada quadro.
5. The quadro descartado SHALL contar como ausente para o limite de dado velho.

**Independent Test**: Publicar no host quadros 480×480, PNG e JPEG truncado numa câmera; conferir no logcat um `DROP` com o motivo certo para cada caso e `STALE` depois do limite.

---

### P1: Falha do dispositivo de vídeo não derruba a ponte ⭐ MVP

**User Story**: Como integrador, quero que um dispositivo de vídeo ausente afete só a câmera dele, para que a telemetria siga funcionando.

**Why P1**: Um único processo ROS alimenta VHAL e EVS (AD-012); uma câmera não pode derrubar o resto.

**Acceptance Criteria**:

1. IF o dispositivo de vídeo de uma câmera não abre ou falha na escrita THEN a ponte SHALL registrar `CAM <pos> DEVICE_UNAVAILABLE` e tentar de novo a cada 1 s, sem encerrar o processo.
2. WHILE o dispositivo de uma câmera está indisponível, as outras câmeras e as propriedades do VHAL SHALL seguir sem interrupção.

**Independent Test**: Iniciar a ponte com o mapa apontando uma câmera para um dispositivo inexistente; as outras 3 e o VHAL funcionam e o logcat mostra `DEVICE_UNAVAILABLE`.

---

### P2: Câmeras no mapa do veículo

**User Story**: Como integrador, quero declarar tópico, posição EVS, resolução e limite de dado velho de cada câmera no mapa do veículo para trocar de veículo ou de fonte sem recompilar.

**Why P2**: O mapa tabelado é regra do projeto (AGENTS.md §1.1); o P1 roda com um mapa só.

**Acceptance Criteria**:

1. The ponte SHALL ler das seções de câmera do mapa do veículo selecionado (`vendor.vipee.vehicle`) o tópico, a posição EVS, a largura, a altura e o limite de dado velho de cada câmera.
2. IF a seção de câmeras do mapa é inválida (posição EVS desconhecida ou repetida, largura ou altura ≤ 0, limite ≤ 0) THEN a ponte SHALL não abrir nenhuma câmera e registrar `CAM MAP_INVALID`, mantendo o VHAL como define a `ct-speed-slice`.
3. WHERE o mapa do veículo não declara câmeras a ponte SHALL rodar só a telemetria, sem erro.

**Independent Test**: Trocar `vendor.vipee.vehicle` entre `commander` e `cat793f` e reiniciar a ponte; as câmeras seguem o mapa de cada um sem recompilar.

---

### P2: Instalação reversível no emulador compartilhado

**User Story**: Como dono do emulador compartilhado, quero instalar e remover o caminho de câmeras da ponte sem perder os MP4 que o AVM e o CalibDroid usam hoje.

**Why P2**: O emulador serve a outros projetos (SHARED.md).

**Acceptance Criteria**:

1. WHEN a instalação roda THEN as 4 câmeras SHALL ficar disponíveis ao `CarEvsManager` como `FRONTVIEW`, `REARVIEW`, `LEFTVIEW` e `RIGHTVIEW`.
2. WHEN a remoção roda THEN o emulador SHALL voltar à HAL EVS e à configuração de câmeras anteriores à instalação.
3. The caminho de câmeras SHALL rodar em SELinux Enforcing sem negação (`avc: denied`) nos domínios da ponte e da HAL EVS.

**Independent Test**: Instalar, abrir as 4 câmeras, remover e conferir que os MP4 voltam.

---

## Edge Cases

- WHEN o bag recomeça no `--loop` (salto do `header.stamp` para trás) THEN a ponte SHALL seguir entregando os quadros, porque o carimbo do EVS é o da chegada (AD-010).
- IF duas mensagens chegam à mesma câmera antes de a anterior ser escrita THEN a ponte SHALL entregar só a mais nova, descartando a outra sem log.
- WHEN nenhum cliente EVS tem a câmera aberta THEN a ponte SHALL seguir aceitando quadros sem acumular memória.

---

## Requirement Traceability

| Requirement ID | Story | AC | Phase | Status |
| -------------- | ----- | -- | ----- | ------ |
| CAM-01 | P1: 4 câmeras no EVS | 1 | Tasks | In Progress |
| CAM-02 | P1: 4 câmeras no EVS | 2 | Tasks | Implemented |
| CAM-03 | P1: 4 câmeras no EVS | 3 | Tasks | Implemented |
| CAM-04 | P1: 4 câmeras no EVS | 4 | Tasks | In Progress |
| CAM-05 | P1: 4 câmeras no EVS | 5 | Tasks | Implemented |
| CAM-06 | P1: Sem dado não congela | 1 | Tasks | Implemented |
| CAM-07 | P1: Sem dado não congela | 2 | Tasks | Implemented |
| CAM-08 | P1: Sem dado não congela | 3 | Tasks | Implemented |
| CAM-09 | P1: Quadro inválido | 1 | Tasks | Implemented |
| CAM-10 | P1: Quadro inválido | 2 | Tasks | Implemented |
| CAM-11 | P1: Quadro inválido | 3 | Tasks | Implemented |
| CAM-12 | P1: Quadro inválido | 4 | Tasks | Implemented |
| CAM-13 | P1: Quadro inválido | 5 | Tasks | In Progress |
| CAM-14 | P1: Falha do dispositivo | 1 | Tasks | Implemented |
| CAM-15 | P1: Falha do dispositivo | 2 | Tasks | Implemented |
| CAM-16 | P2: Câmeras no mapa | 1 | Tasks | In Progress |
| CAM-17 | P2: Câmeras no mapa | 2 | Tasks | In Progress |
| CAM-18 | P2: Câmeras no mapa | 3 | Tasks | In Progress |
| CAM-19 | P2: Instalação reversível | 1 | Tasks | Implemented |
| CAM-20 | P2: Instalação reversível | 2 | Tasks | Implemented |
| CAM-21 | P2: Instalação reversível | 3 | Tasks | Implemented |
| CAM-22 | Edge: loop do bag | - | Tasks | In Progress |
| CAM-23 | Edge: só o mais novo | - | Tasks | In Progress |
| CAM-24 | Edge: sem cliente | - | Tasks | In Progress |

**Coverage:** 24 total, 0 mapped to tasks, 24 unmapped (Tasks ainda não feito).

---

## Success Criteria

- [ ] Com o bag tocando em loop e `vendor.vipee.vehicle=commander`, um cliente `CarEvsManager` recebe as 4 câmeras em 1280×720 a ≥ 8 quadros/s cada, por 60 s, e o AVM mostra a vista de cima com as imagens do bag.
- [ ] Parando o bag, nenhuma câmera entrega quadro novo depois de 600 ms e o logcat tem `CAM <pos> STALE` das 4; voltando o bag, as 4 retomam sem reabrir.
- [ ] O `test-e2e.sh` da `ct-speed-slice` passa com as 4 câmeras ligadas.
- [ ] Zero `avc: denied` nos domínios da ponte e da HAL EVS, em Enforcing.

---

## Contrato com o simulador

Junto com esta feature, `docs/sim-contract.md` §3.4 passa a dizer: resolução de referência 1280×720 (o simulador será ajustado); FOV do CAT793F indefinido; FOV do Commander ~192°; o rig da tabela é o do CAT793F, e o bag de calibração é do Commander, com os mesmos nomes de tópico.
