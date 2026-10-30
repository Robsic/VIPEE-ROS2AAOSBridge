# VIPEE ROS2AAOSBridge

Ponte ROS2 ➔ Android Automotive (AAOS). Lê tópicos, serviços e ações do ROS2 e os injeta no AAOS, para que o dashboard do veículo (AAOS) rode integrado a um simulador de modo transparente. A simulação é o ambiente de desenvolvimento de funções que depois vão para um veículo real, então o lado AAOS não pode saber que a fonte é simulada.

Mapeamento de sinais (alvo):

* Telemetria do simulador ➔ **VHAL** (propriedades padrão e vendor).
* Câmeras ➔ **EVS** (HAL EVS virtual).
* Radares, GPS e demais sensores ➔ HAL/serviço correspondente do AAOS (a definir por feature).

Diretrizes normativas para agentes de IA e desenvolvedores:

## 1\. Arquivos de Contexto

Consulte antes de alterações de arquitetura, contratos de sinal ou fronteiras de middleware:

* [**.specs/STATE.md**](./.specs/STATE.md): Decisões de projeto (`AD-NNN`) e Handoff da última sessão. As decisões são normativas; contrariar uma exige registrar uma nova. Ao retomar trabalho, leia o `STATE.md` e reconcilie o handoff com o git antes de agir.
* [**.specs/features/**](./.specs/features/): Uma pasta por feature, com `spec.md` (requisitos em EARS), `context.md`, `design.md`, `tasks.md` e `validation.md`.
* [**Refs/**](./Refs/): Documentação adicional (diagramas, manuais, listas de sinais).
* Projetos irmãos (em `../`):
  * `VIPEE-CalibDroid`: app AAOS de calibração. **Consumidor direto desta ponte**: AD-016 declara que a ponte ROS2 ➔ EVS e a HAL EVS virtual são um projeto próprio, este. AD-027 define como o app lê as câmeras (`CarEvsManager`, 4 tipos FRONTVIEW/REARVIEW/LEFTVIEW/RIGHTVIEW). AD-010 define a tela de engenharia disparada por propriedade vendor VHAL. Sua `tools/android_sync.sh` mostra como sincronizar módulos para `$ANDROID_BUILD_TOP/device/generic/car/emulator/evs/`.
  * `VIPEE-SynthCamStream`: publica o WoodScape como 4 tópicos ROS2 de imagem; é a fonte de câmeras para teste. Precedente da política de adapter: a interface é nossa, o middleware é detalhe.
  * `VIPEE-UndistortDemo`: runtime AVM (consumidor final das câmeras e dos YAML de calibração).

### 1.1 Restrições herdadas (não reabrir sem nova AD)

* **ROS2 no Android (revisa CalibDroid AD-011).** ROS2 Jazzy com CycloneDDS cross-compila com o NDK r27 e roda nativo no emulador AAOS 16 (AD-011; receita em `tools/jazzy/`, origem em `Refs/handoff-ros2-jazzy-aaos.md`). Um único processo ROS no Android, serviço vendor no domínio SELinux `vendor_vipee_ros2`, alimenta VHAL e EVS (AD-012, aceita no emulador; IPC até as HALs e RPi5 pendentes). O núcleo da ponte continua sem depender de ROS nem de Android.
* O `evsmanagerd` só aceita UIDs `AID_AUTOMOTIVE_EVS`/`SYSTEM`/`ROOT`/`GRAPHICS` (CalibDroid AD-027). Uma HAL EVS virtual precisa respeitar isso; app comum não injeta câmera.
* Etapas de validação (CalibDroid AD-017): emulador AAOS ➔ mula RPi5 com AAOS ➔ veículo de engenharia (Qualcomm SA6155P). A ponte deve funcionar nas três sem mudar o contrato.
* Frames sempre nomeados: em qualquer pose, ponto ou vetor, diga o frame e a direção da transformação (`a_from_b`) e as unidades (m, m/s, rad, px). Frame do veículo: origem na projeção no solo da interseção do eixo dianteiro com a linha central; X à frente, Y à esquerda, Z para cima (CalibDroid AD-004). Cuidado com a conversão para o frame do ROS (REP-103) e do Android sensors/VHAL.
* A tradução de sinais é **tabelada e versionada** (mapa tópico ROS2 ➔ propriedade VHAL com unidade, escala, taxa e política de dado velho), nunca espalhada em código.

### 1.2 Documentação Externa (context7)

Consulte obrigatoriamente o MCP **context7** para APIs e boas práticas (ROS2 Jazzy `rclcpp`/`rclpy`/`sensor_msgs`, CycloneDDS, C++17, CMake, vcpkg, Android NDK/Soong, Kotlin, AAOS `VehicleHal`/`CarPropertyManager`, EVS HAL AIDL, `CarEvsManager`). Valide assinaturas e breaking changes antes e depois de codificar. A API do VHAL e do EVS muda entre releases do Android: confirme a versão do AOSP em uso.

## 2\. Fluxo de trabalho: tlc-spec-driven

Toda feature passa pela skill **tlc-spec-driven** (`.claude/skills/`): Specify ➔ Design ➔ Tasks ➔ Execute, com profundidade ajustada ao escopo. Os gates determinísticos vivem em `<skill-dir>/scripts/` e rodam antes de cada revisão humana:

```bash
  python3 <skill-dir>/scripts/validate_spec.py <feature>
  python3 <skill-dir>/scripts/validate_tasks.py <feature>
  python3 <skill-dir>/scripts/check_commit.py --message "<msg>"
  python3 <skill-dir>/scripts/validate_state.py <feature>
  ```

Saída não-zero significa parar e corrigir antes de seguir.

### 2.1\. Implementação multiagentes com isolamento de contexto

Features com mais de ~8 tasks são executadas por sub-agentes, **após o usuário aceitar a oferta**:

1. **Workers**: um por lote de ~7 tasks, formado por fases inteiras consecutivas. Os lotes rodam em sequência, sem sub-agentes aninhados.
2. **Verifier**: roda sempre depois da última task, com contexto novo (autor ≠ verificador). Faz a checagem ancorada no spec, roda o sensor de discriminação (mutantes em cópia isolada, nunca `git stash`), valida a corretude de API com o context7 e escreve o `validation.md`.

Proibido contexto misto. Relate causas de falhas e ações corretivas a cada ciclo; o laço correção ➔ reverificação para em 3 iterações e escala para o usuário.

### 2.2\. Ajuste de modelo/effort

- Specify, Design e fases de domínio central (mapeamento de sinais, sincronização, HAL): modelos mais capazes (Claude Opus 5 high effort ou similar).
- Fases mecânicas (wiring, config, CMake/Soong, tabelas já decididas): modelos mais rápidos (Claude Sonnet 5 high effort ou similar). EVITE Gemini para geração de código, ele tem dificuldade em seguir specs.
- Verifier nunca é o tier mais barato (Claude Opus 5 medium effort ou similar). Na dúvida, suba o tier.

## 3\. Robustez de testes

* Siga **RED-GREEN-REFACTOR** estrito (falha genuína comprovada antes do código de produção).
* Testes derivam dos critérios de aceite do spec e assertam o resultado definido no spec. Nunca espelham a implementação.
* Cubra unitários, integração e cenários de borda/erro.
* O núcleo é testado sem ROS e sem Android, com fontes e sinks fakes que implementam a **mesma interface própria**. Nunca mocke `rclcpp` nem a HAL.
* Teste de integração para cada dependência externa (ROS2 real, `adb`/emulador), marcado e separado da suíte rápida.
* **Fail-Loud:** sinal ausente, fora de faixa, dado velho, unidade ou frame desconhecido aborta ou sinaliza com código de erro nomeado. Nunca mascare falhas e nunca injete valor plausível-mas-errado no AAOS: o dashboard confia no que a ponte entrega, como confiaria no veículo. Em dúvida, marque a propriedade como indisponível (`UNAVAILABLE`/`ERROR`), não invente valor.
* Nunca enfraqueça, pule ou apague teste para fazê-lo passar. Mantenha a suíte 100% verde antes de concluir qualquer task.

### 3.1\. Gate de lint e formatação

`.clang-format` (`BasedOnStyle: Google`) e `.clang-tidy` (`clang-analyzer-*`, `bugprone-*`, `performance-*`, avisos como erro), copiados do CalibDroid. Como o repositório nasce sob o gate, ele roda **por arquivo inteiro**. `clang-tidy` exige `build/compile_commands.json`, exportado pelo preset CMake. Python (se houver): `ruff` configurado em `pyproject.toml`; Kotlin: `ktlint`.

## 4\. Documentation as code

* Padrão **DOXYGEN** (`@brief`, `@param`, `@return`, `@note`) na superfície pública de headers (classes, funções, structs, enums, constantes). Kotlin: KDoc. Python: docstring.
* Detalhe assinaturas, formatos, pré-condições e semântica, incluindo frame, direção da transformação e unidades de cada sinal.
* **Exceções**: arquivos de teste não levam Doxygen nem docstring; helpers de namespace anônimo em `.cpp` levam no máximo um comentário `//` quando o nome não basta.

## 5\. Commits e Versionamento

* **Conventional Commits 1.0.0** (`feat(scope): ...`, `fix`, `test`, `refactor`, `docs`, `perf`, `build`, `ci`, `chore`), em minúscula, sem ponto final, validados por `check_commit.py`.
* **Um commit atômico por task.** Marque a task como concluída no `tasks.md` **antes** do commit, dentro do mesmo commit. Nunca agrupe tasks.
* Aprovar o `tasks.md` autoriza a implementação local e os commits locais das tasks. Fora do fluxo de tasks (docs, chore, correção avulsa), cada commit pede confirmação: `git status`, mensagem proposta, lista de arquivos, aguardar ok.
* **Proibido push, force-push, merge na `main`, deploy, flash de imagem ou outra operação remota/destrutiva sem aprovação explícita do usuário para aquela ação.** Isso inclui `adb` contra dispositivo físico e qualquer escrita em `$ANDROID_BUILD_TOP`.
* Mudança de versão (MAJOR.MINOR.PATCH) atualiza o arquivo de build e o `README.md` juntos. Mudança MAJOR/MINOR cria branch nova e consulta o usuário sobre o merge.

### 5.1 Sanitização de contexto

Ao fim de cada fase (Specify, Design, Tasks, cada lote de Execute), atualize o Handoff do `.specs/STATE.md` e compacte o contexto antes de prosseguir.

## 6\. Ferramental (ambiente de desenvolvimento)

### 6.1 Versionado no repo

* **tlc-spec-driven**: skill local em `.claude/skills/tlc-spec-driven/`; lock em `.agents/.skill-lock.json`. Dados do projeto em `.specs/`.
* **Lint**: `.clang-format`, `.clang-tidy`.
* **CMake + vcpkg**: preset e manifest espelhando `../VIPEE-CalibDroid/CMakePresets.json` e `vcpkg.json` (a criar na task de scaffolding da primeira feature C++).

### 6.2 Configuração global e variáveis

* **MCP `context7`**: remoto (`https://mcp.context7.com/mcp`).
* **Variáveis (`~/.bashrc`)**:

```bash
  export VCPKG_ROOT=$HOME/vcpkg          # vcpkg existe em ~/vcpkg; a variável não está exportada no shell atual
  export ANDROID_BUILD_TOP=$HOME/aosp    # árvore AOSP local
  export ROS_DOMAIN_ID=25                # mesmo domínio do SynthCamStream/simulador
  ```

* **ROS2 Jazzy** só no contêiner `vipee/ros2-jazzy-android` (Ubuntu 24.04 + NDK r27), via `tools/jazzy/run.sh`; workspace em `~/ros2_android_ws`, fora do repo. O host é Ubuntu 22.04 com Humble em `/opt/ros/humble`, que fica para outros projetos e não deve ser usado aqui. **Cuidado com contaminação de ambiente:** o build cruzado não pode enxergar nenhum ROS do host nem o `/opt/ros/jazzy` do contêiner (por isso o `--entrypoint ''`); o shell do host ainda sourceia um overlay do `SIRV-Simulator`. Confira com `llvm-readelf -d` que nenhum binário Android pede lib versionada (`.so.N`) de fora do install Android.
* **Disco:** o VHDX do WSL fica no C:, com cerca de 60 GB livres (2026-10). Builds AOSP grandes podem encher o C: e travar o WSL; confira `df` do C: (`/mnt/c`), não o do `/`.
* **Android**: AOSP em `~/aosp`. O emulador AAOS roda **dentro do WSL**, headless com KVM (`~/aosp-scripts/emu.sh`, `sdk_car_x86_64-bp4a-userdebug`); use o `adb` do AOSP (`~/aosp/out/host/linux-x86/bin/adb`). Do emulador, `10.0.2.2` é o loopback do WSL, então um contêiner com `--network host` é alcançável direto. Ver `../VIPEE-CalibDroid/tools/android_sync.sh`.
* **Python** 3.10 (Ubuntu 22.04, o mesmo do ROS2 Humble) e `uv` para scripts/ferramentas auxiliares.

### 6.3 Setup rápido

1. Requisitos: GCC/Clang (C++17), CMake ≥ 3.21, Ninja, Python 3.10, `uv`, vcpkg bootstrapado, Docker (contêiner Jazzy), `clang-format`/`clang-tidy`, `adb`.
2. Exportar as variáveis da seção 6.2.
3. Build e testes (depois da task de scaffolding):

```bash
   cmake --preset default && cmake --build --preset default && ctest --preset default
   ```

## 7\. Decisões de arquitetura

Transporte, local no Android, VHAL, EVS, relógio e ROS no Android estão decididos em `.specs/STATE.md` (AD-001 a AD-012; AD-011 e AD-012 substituem AD-001 a AD-003 e AD-006). O contrato com o simulador está em [`docs/sim-contract.md`](./docs/sim-contract.md).
