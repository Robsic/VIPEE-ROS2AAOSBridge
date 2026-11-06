# rpi-image Specification

## Problem Statement

A ponte só roda no emulador x86_64, instalada por `adb push` (`install-vendor.sh`). O demo de 2026-11-28 é numa mula Raspberry Pi 5 com AAOS 16, e a combinação RPi + AAOS + ponte nunca rodou (risco principal do marco no `STATE.md`). As RPi5 estão em compra; há uma RPi4 disponível. Esta feature gera uma imagem AAOS gravável em cartão SD com a ponte embutida, valida na RPi4 e deixa a imagem da RPi5 pronta da mesma árvore, com um handoff de instalação e execução para quem tiver a placa na mão.

## Goals

- [ ] Uma imagem `aosp_rpi4_car` (raspberry-vanilla `android-16.0`) com a ponte embutida, gravada num SD, dá boot na RPi4 e, com o simulador ou o `fake_sim` no host da rede, entrega velocidade, marcha e freio de mão ao `CarService`.
- [ ] A mesma árvore gera a imagem `aosp_rpi5_car`, pronta para quando as RPi5 chegarem.
- [ ] Com o `v4l2loopback` no kernel da placa, as 4 câmeras do bag chegam ao EVS na RPi4 como no emulador.
- [ ] Um handoff em `Refs/` permite a outra pessoa gravar o SD, ligar a placa ao host e rodar o teste sem ajuda.

## Out of Scope

| Feature | Reason |
| ------- | ------ |
| Validação na RPi5 | As placas ainda não chegaram; a imagem sai pronta, o teste fica para quando chegarem |
| Mudar o contrato com o simulador | A ponte e o mapa são os mesmos do emulador; muda só onde o host está na rede |
| Desempenho e otimização na placa | Esta feature mede latência e CPU para registro, sem metas novas; ajuste fica para depois do primeiro boot |
| AVM e CalibDroid na imagem | São projetos próprios; a imagem traz só a ponte. Instalar os apps deles na placa é trabalho deles, com o handoff desta feature |
| OTA, assinatura de release, `user` build | Imagem de teste `userdebug`, como no emulador |
| Veículo de engenharia (SA6155P) | Terceira etapa de validação, fora desta feature |
| Adb pela rede na placa | O handoff de 2026-10 registrou que o adb por Wi-Fi não funcionou no RPi4; o roteiro usa adb por USB |

---

## Assumptions & Open Questions

| Assumption / decision | Chosen default | Rationale | Confirmed? |
| --------------------- | -------------- | --------- | ---------- |
| Base AOSP da imagem | raspberry-vanilla `android-16.0` sobre `android-16.0.0_r4`, `lunch aosp_rpi4_car-bp4a-userdebug` e `aosp_rpi5_car-bp4a-userdebug` | O branch `android-16.0` do raspberry-vanilla usa `android-16.0.0_r4`, a mesma base do emulador; a AD-005 citava `bp2a`/`r1` e precisa ser revista (nova AD) | n |
| Árvore de build | Árvore própria `/home/wendell/aosp-rpi` na VM `vipee`, rasa (`--depth=1` + `remove_projects.xml`) | O manifesto do raspberry-vanilla troca `build/make`, `build/soong`, `minigbm`, `drm_hwcomposer` e outros; misturar com a árvore do emulador quebraria AVM e CalibDroid | y |
| Placas | Imagem das duas (RPi4 e RPi5); validação só na RPi4 | RPi4 disponível; RPi5 em compra (Wendell, 2026-11-04) | y |
| Ponte na imagem | Libs ROS, `vipee_bridge`, VHAL da ponte, HAL `vipee-evs-v4l2`, política SELinux e `.rc` entram na imagem pelo build (módulos Soong), não por `adb push` | Imagem gravada já pronta para o demo; o `adb remount` não sobrevive a regravar o SD | n |
| `v4l2loopback` | O kernel pré-compilado do raspberry-vanilla (`android16-6.12`) não traz o módulo; a ponte compila o `v4l2loopback.ko` fora da árvore contra o kernel da placa e o embute no vendor | Lista de módulos de `android_device_brcm_rpi4-kernel` sem `v4l2loopback` (2026-11-04); o caminho de vídeo da AD-015 depende dele | n |
| Endereço do host | Configurável na placa sem recompilar (propriedade de sistema lida na partida da ponte); padrão vazio = ponte não disca e loga o erro | No emulador o host é sempre `10.0.2.2`; na rede local muda por instalação | n |
| Host atrás do WSL | O handoff explica como expor a porta 7400 do contêiner no WSL à rede local (rede espelhada do WSL ou `netsh portproxy`) | O simulador e o bag rodam no WSL; a placa está na LAN | n |
| Telemetria na placa | Mesmos critérios da `ct-speed-slice`, mas a latência só é medida e registrada, sem limite novo | O caminho de rede é outro (LAN em vez do NAT do emulador); um limite agora seria chute | n |
| Gravação do SD | Feita por Wendell (ou quem tiver a placa) seguindo o handoff; a ponte não grava SD nem roda `adb` contra a placa sem aprovação | AGENTS.md §5 (adb contra dispositivo físico e flash exigem aprovação) | y |

**Open questions:** none - os itens `n` acima são defaults técnicos, revistos no Design.

---

## User Stories

### P1: Imagem RPi4 com a ponte que dá boot ⭐ MVP

**User Story**: Como integrador do demo, quero gravar uma imagem AAOS no SD da RPi4 com a ponte já instalada para testar o caminho simulador ➔ placa sem montar nada à mão.

**Why P1**: Sem imagem não há teste na placa; é o risco principal do marco.

**Acceptance Criteria**:

1. WHEN o build da árvore `aosp-rpi` termina THEN o processo SHALL produzir um arquivo de imagem gravável em SD para `aosp_rpi4_car` (saída do `rpi4-mkimg.sh`).
2. WHEN a RPi4 dá boot com a imagem THEN o Android SHALL chegar a `sys.boot_completed=1` em SELinux `Enforcing`.
3. WHEN o boot termina THEN o serviço `vipee_bridge` SHALL estar rodando no domínio `vendor_vipee_ros2` e o VHAL da ponte SHALL estar registrado no lugar do VHAL padrão da placa.
4. The imagem SHALL trazer todas as libs da ponte em `/vendor`, sem nenhuma lib versionada (`.so.N`) de fora do install Android (`llvm-readelf -d`).
5. IF a propriedade de endereço do host está vazia THEN a ponte SHALL logar `DDS_PEER_UNSET` e manter as propriedades da ponte `UNAVAILABLE`.

**Independent Test**: gravar o SD, ligar a RPi4 por USB, `adb shell getprop sys.boot_completed`, `getenforce`, `ps -AZ | grep vipee`.

---

### P1: Telemetria do host chega ao CarService na RPi4 ⭐ MVP

**User Story**: Como integrador, quero que velocidade, marcha e freio de mão publicados no host da rede cheguem ao `CarService` da RPi4 para provar o caminho de dados do demo.

**Why P1**: É a fatia vertical mínima na placa real (a `ct-speed-slice` no emulador).

**Acceptance Criteria**:

1. WHEN o endereço do host é definido na placa e a ponte reinicia THEN a ponte SHALL estabelecer a conexão DDS por TCP com o host na porta 7400.
2. WHEN o `fake_sim` publica velocidade, marcha e freio de mão no host THEN `PERF_VEHICLE_SPEED`, `GEAR_SELECTION` e `PARKING_BRAKE_ON` SHALL chegar ao `CarService` da RPi4 com os valores do mapa do veículo.
3. WHEN a fonte para de publicar THEN as propriedades SHALL virar `UNAVAILABLE` no limite de dado velho do mapa (o mesmo do emulador).
4. The teste SHALL registrar a latência medida host ➔ `CarService` na RPi4 no `validation.md`, sem limite de aprovação.

**Independent Test**: `fake_sim` no host, `dumpsys car_service get-property-value` na placa, como em `tools/emu/test-e2e.sh`.

---

### P2: Câmeras do bag no EVS da RPi4

**User Story**: Como desenvolvedor do AVM e do CalibDroid, quero as 4 câmeras do bag no EVS da RPi4 para rodar meus apps na placa como rodo no emulador.

**Why P2**: O demo usa câmeras, mas depende de compilar um módulo de kernel; a telemetria prova a placa antes.

**Acceptance Criteria**:

1. WHEN a placa dá boot THEN os dispositivos `/dev/video40..43` (AD-021) SHALL existir, criados pelo `v4l2loopback` da imagem.
2. WHEN o bag de calibração toca no host com o veículo `commander` THEN um cliente EVS na RPi4 SHALL receber as 4 câmeras em 1280×720, cada uma a pelo menos 8 quadros/s.
3. IF o módulo `v4l2loopback` não carrega THEN a ponte SHALL logar `DEVICE_UNAVAILABLE` por câmera e a telemetria SHALL continuar funcionando.

**Independent Test**: `vipee_evs_probe 5 /dev/video40 /dev/video41 /dev/video42 /dev/video43` na placa com o bag tocando.

---

### P2: Imagem RPi5 da mesma árvore

**User Story**: Como integrador, quero a imagem da RPi5 pronta antes das placas chegarem para não perder dias de build na reta final.

**Why P2**: O demo é na RPi5; a RPi4 só antecipa o risco.

**Acceptance Criteria**:

1. WHEN o build `aosp_rpi5_car` termina THEN o processo SHALL produzir a imagem gravável da RPi5 (saída do `rpi5-mkimg.sh`) com os mesmos módulos da ponte da RPi4.
2. WHERE o `v4l2loopback` foi compilado para o kernel da RPi5 a imagem SHALL embuti-lo como na RPi4.

**Independent Test**: listar os módulos da ponte no `vendor.img` da RPi5 (`debugfs`/`lpunpack`) e comparar com a RPi4.

---

### P1: Handoff de instalação e execução ⭐ MVP

**User Story**: Como Wendell ou outro membro do time com a placa na mão, quero um roteiro para gravar o SD, ligar a placa ao host e rodar o teste, para não depender da sessão de quem gerou a imagem.

**Why P1**: Quem gera a imagem não tem a placa; sem roteiro, a imagem não é testada.

**Acceptance Criteria**:

1. The handoff `Refs/handoff-rpi-image.md` SHALL cobrir: onde baixar a imagem, como gravar o SD, ligação física (HDMI, USB, rede), como definir o endereço do host, como expor a porta 7400 do WSL na rede local, como rodar o `fake_sim` e o bag, e o que conferir (boot, SELinux, serviço, propriedades, câmeras).
2. The handoff SHALL listar, para cada passo de conferência, o comando e a saída esperada.
3. The handoff SHALL incluir uma seção de problemas conhecidos com o sintoma e a correção de cada um.

**Independent Test**: outra pessoa (ou sessão) segue o handoff do zero até ver a velocidade no `CarService`.

---

## Edge Cases

- IF a placa não alcança o host na porta 7400 THEN a ponte SHALL manter as propriedades `UNAVAILABLE` e logar a falha de conexão, sem reiniciar em laço.
- IF o build de uma das placas falha THEN o processo SHALL entregar a imagem da outra e registrar a falha no `STATE.md`.
- WHEN o mesmo SD é regravado com uma imagem nova THEN nenhuma configuração da ponte SHALL depender do que estava no SD antes, exceto o endereço do host, que o handoff manda redefinir.

---

## Implicit-Requirement Dimensions

| Dimension | Resolution |
| --------- | ---------- |
| Input validation & bounds | Endereço do host inválido ou vazio: RPI-05 e edge case de conexão |
| Failure / partial-failure states | Módulo de vídeo ausente (RPI-12), build de uma placa falha (edge case) |
| Idempotency / retry / duplicate handling | Regravar o SD (edge case); a reconexão DDS é a do CycloneDDS, igual ao emulador |
| Auth boundaries & rate limits | N/A because a imagem é `userdebug` de teste numa rede local; o domínio SELinux da ponte é o mesmo do emulador (RPI-02, RPI-03) |
| Concurrency / ordering | N/A because a ordem de partida é a do `init`, a mesma do emulador |
| Data lifecycle / expiry | Dado velho vira `UNAVAILABLE` (RPI-08) |
| Observability | Logs `DDS_PEER_UNSET`, `DEVICE_UNAVAILABLE`, `STATS` da ponte; latência registrada (RPI-09) |
| External-dependency failure | Host fora do ar (edge case); simulador parado (RPI-08) |
| State-transition integrity | N/A because a feature não cria estados novos na ponte; reusa as máquinas da `ct-speed-slice` e da `cam-evs-slice` |

---

## Requirement Traceability

| Requirement ID | Story | Phase | Status |
| -------------- | ----- | ----- | ------ |
| RPI-01 | P1: Imagem RPi4 | 1 | Pending |
| RPI-02 | P1: Imagem RPi4 | 2 | Pending |
| RPI-03 | P1: Imagem RPi4 | 3 | Pending |
| RPI-04 | P1: Imagem RPi4 | 4 | Pending |
| RPI-05 | P1: Imagem RPi4 | 5 | Pending |
| RPI-06 | P1: Telemetria na RPi4 | 1 | Pending |
| RPI-07 | P1: Telemetria na RPi4 | 2 | Pending |
| RPI-08 | P1: Telemetria na RPi4 | 3 | Pending |
| RPI-09 | P1: Telemetria na RPi4 | 4 | Pending |
| RPI-10 | P2: Câmeras na RPi4 | 1 | Pending |
| RPI-11 | P2: Câmeras na RPi4 | 2 | Pending |
| RPI-12 | P2: Câmeras na RPi4 | 3 | Pending |
| RPI-13 | P2: Imagem RPi5 | 1 | Pending |
| RPI-14 | P2: Imagem RPi5 | 2 | Pending |
| RPI-15 | P1: Handoff | 1 | Pending |
| RPI-16 | P1: Handoff | 2 | Pending |
| RPI-17 | P1: Handoff | 3 | Pending |

**Coverage:** 17 total, 0 mapped to tasks, 17 unmapped ⚠️

---

## Success Criteria

- [ ] A RPi4 com a imagem nova mostra a velocidade do `fake_sim` no `CarService` seguindo só o handoff.
- [ ] A imagem da RPi5 existe e traz os mesmos módulos da ponte.
- [ ] Com o bag tocando, as 4 câmeras chegam ao EVS na RPi4 a ≥ 8 quadros/s.
