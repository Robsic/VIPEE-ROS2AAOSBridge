# Prompt: handoff da compilação do ROS2 no Android

Enviado em 2026-10-28 ao colega do simulador, para o agente dele gerar o handoff da abordagem ROS2-no-AAOS (ver Handoff "Pivô em avaliação" em `.specs/STATE.md`). O handoff recebido está em `Refs/handoff-ros2-jazzy-aaos.md` (PDF original ao lado).

```markdown
Gere um handoff técnico, em Markdown, da compilação e execução do ROS2 como módulo no Android Automotive (AAOS) feita neste repositório. O leitor é outro time (projeto VIPEE-ROS2AAOSBridge) que vai reproduzir a abordagem do zero, numa máquina diferente, e embutir o ROS2 dentro de HALs de vendor (VHAL e EVS).

Regras:
- Use apenas o que está no código, scripts, configs, logs e histórico git deste repositório. Para cada afirmação, cite o arquivo:linha ou o comando de origem.
- O que não puder ser confirmado, marque como "NÃO VERIFICADO" e diga por quê. Não preencha lacunas com suposições.
- Comandos exatos, copiáveis, na ordem em que foram executados. Inclua versões (tags, commits, hashes).
- Não inclua segredos, chaves, IPs internos nem caminhos pessoais; troque por placeholders.

Estrutura obrigatória:

1. Resumo: o que funciona hoje, em uma frase, e a evidência (log, saída de `ros2 topic echo`, captura).
2. Ambiente: SO do host, versão do AOSP (tag do manifest), produto/lunch target, arquiteturas (x86_64 emulador, arm64), versão do NDK se usado, toolchain.
3. ROS2: distro, lista de pacotes compilados com versões/commits, RMW usado (Fast DDS, Cyclone DDS, Zenoh etc.) e por quê.
4. Sistema de build:
   - Soong (Android.bp), NDK/CMake, colcon cruzado ou outro? Mostre os arquivos de build relevantes.
   - Qual libc++ é usada (`c++_static`/`c++_shared` do NDK ou a libc++ da plataforma)? Os binários linkam em módulo de plataforma (vendor/system) ou só em app?
   - Patches aplicados ao ROS2 ou dependências (por exemplo `rcutils`, `rclcpp` signal handler, `spdlog`, `tinyxml2`, `console_bridge`): liste cada patch, o motivo e o diff.
5. Mensagens: como o type support (rosidl, C/C++, introspection ou typesupport_fastrtps) é gerado para o Android; como adicionar um pacote de mensagens próprio (ex.: `vipee_vehicle_msgs`).
6. Execução no Android:
   - Onde o código roda: app (UID), processo nativo `system` ou `vendor`, serviço init (`.rc`)?
   - Domínio SELinux e regras adicionadas (sockets UDP, multicast, `net_domain`), permissões Android necessárias.
   - Como os binários/bibliotecas entram na imagem (PRODUCT_PACKAGES, push via adb, APK).
7. Rede e descoberta DDS:
   - Topologia testada (emulador ou dispositivo físico; onde rodava o publicador ROS2; WSL, Linux nativo, rede).
   - Como a descoberta funcionou: multicast, peers unicast, discovery server, XML de perfil do RMW? Anexe os arquivos de configuração.
   - `ROS_DOMAIN_ID`, `ROS_LOCALHOST_ONLY`, portas usadas, configurações de rede do emulador (redirecionamentos, TAP, `adb reverse`).
8. Desempenho medido (se houver): latência, taxa, uso de CPU/memória, tamanho dos binários. Se mediu imagem, informe resolução, encoding e taxa alcançadas.
9. Limitações e problemas conhecidos: o que não funciona, o que é instável, gambiarras temporárias.
10. Passo a passo de reprodução: do clone até ver uma mensagem `geometry_msgs/msg/TwistStamped` publicada no host chegando a um assinante no Android, com a saída esperada de cada passo.
11. Perguntas em aberto e próximos passos sugeridos.

Salve como `HANDOFF-ros2-android.md` na raiz do repositório e mostre o resumo final no chat.
```

## Por que estes itens

* **4, 6 e 7** cobrem os riscos centrais para nós: libc++ do NDK contra a da plataforma ao embutir o ROS numa HAL vendor (CalibDroid AD-026), SELinux para o DDS, e descoberta DDS atravessando o emulador e o WSL em modo NAT.
* **10** vira o roteiro do spike de reprodução.
