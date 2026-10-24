# Contexto compartilhado — RPCS3 e inFAMOUS 2

**Atualizado em:** 24/10/2026
**Uso:** passagem de contexto entre Codex e Claude. Este documento descreve o estado encontrado no checkout e separa alterações aplicadas, medições e hipóteses.

## Resumo do estado atual

- O projeto é um fork local do RPCS3 para investigar melhorias específicas para *inFAMOUS 2*. A meta é melhorar consistência e desempenho sem sacrificar estabilidade, efeitos visuais ou progresso da campanha.
- Repositório: `C:\Users\T-GAMER\rpcs3`; `origin` aponta para `vhd-kilim/rpcs3-infamous2-optimized` e `upstream` para `RPCS3/rpcs3`.
- Checkout atual: branch `codex/rsx-offloader-idle`, baseada no commit `0dec46b12e856756ba04271207a99a1de2208720`. As referências locais `master` e `infamous2/dev` estavam no mesmo commit-base quando conferidas.
- Há **16 arquivos de código/XML** no diff local (13 modificados e 3 novos), ainda sem commit, além deste documento de contexto. Não foi feito push.
- A compilação Release x64 mais recente registrada terminou com código 0 em 24/10/2026 às 11:49. Executável: `C:\Users\T-GAMER\rpcs3\bin\rpcs3.exe`; SHA-256 `F26AE7098D3011B922F253F1EB31A8A4204B11A1BC341CC8CB5EFD0F4F95EAA5`.
- A build anterior, SHA-256 `8AB17185848FB1ABDACAF77432984B90ABF2E30A6997DFBE2CD1817E31C5C5E5`, foi usada na sessão de missão, no perfil de esperas e na captura gráfica. Não encontrei uma medição em jogo ou benchmark feita com a build mais recente `F26AE...`.
- **Não há patch funcional de iluminação, NPCs ou física aplicado ao jogo.** A correção do recorte ainda não foi localizada.
- **Atualização 24/10 13:16 (Claude):** o diff descrito abaixo está no commit local `c5656232c` (sem push). Um A/B controlado de 4 rodadas mostrou **+5,4% de FPS médio** com a mudança do Offloader (31,21 → 32,89). Ver seção "A/B controlado do Offloader".

## Projeto, jogo e ambiente

O alvo analisado é *inFAMOUS 2* para PS3, Title ID `BCUS98125`, atualização `01.04`, PPU hash `PPU-41a7d047c573c78c0307506c9d1b52d15a66e4f5`. As ferramentas de captura de iluminação e RSX verificam essa identidade antes de registrar dados. Não use os resultados para outra versão do jogo sem confirmar o hash.

Hashes registrados na análise anterior:

- EBOOT instalado: SHA-256 `9906A9A4872B2BE5779C15371E21D2F0A3010BAB64544CE2867DD611F0C3AE5D`.
- ELF de análise, extraído de uma cópia: SHA-256 `3BFE08DF4E2ED1201A83BE3D38F13A66E900153DD4071F25A35C3C95A1717298`.

O contexto anterior informa o seguinte hardware: Intel Core i5-14400F, Radeon RX 9060 XT 16 GB, 32 GB DDR4, Windows 10 x64, monitor 2560×1440 e Vulkan. Esses dados vieram do contexto do projeto e não foram rechecados nesta tarefa.

Os arquivos do jogo, firmware e saves ficam locais e fora do Git. Antes de cada sessão, conferir e fazer backup dos saves: o RPCS3 pode alterar `STATE` automaticamente ao iniciar ou encerrar o jogo. Não editar saves sem autorização. A autorização anterior para instalar ferramentas necessárias, inclusive Ghidra quando útil, foi registrada no contexto do projeto; os relatórios consultados não registram instalação do Ghidra até aqui.

### Ferramentas de build

Registro anterior do ambiente: Qt 6.12.0 `msvc2022_64`, Vulkan SDK 1.4.341.1 e LLVM 22.1.8. `C:\Users\T-GAMER\rpcs3-dev-tools\Enter-Rpcs3Dev.ps1` prepara as variáveis usadas pelo build. Uma tentativa anterior encontrou objetos LLVM feitos com MSVC 19.51 e linker VS2022 19.44; a compilação Release x64 de 24/10 foi bem-sucedida, mas confira o log/toolchain antes de trocar o fluxo de build. O `scripts\win_build.ps1` pode limpar parte de `build` e ativar `BUILD_LLVM`; não o execute sem revisar seus efeitos e os caches existentes.

## Alterações no checkout feitas por Codex

As alterações abaixo são o diff local atual, não commits publicados. A mudança de execução é a espera do Offloader; o restante acrescenta observabilidade e arquivos de projeto para analisar o jogo.

### Espera do RSX Offloader

Em `rpcs3/Emu/RSX/RSXOffload.cpp`, quando não há trabalho pendente, o worker deixou de chamar `std::this_thread::yield()` em repetição e passou a esperar em `thread_ctrl::wait_on(m_work_queue)`. Também foi incluído um escopo de medição para sincronização do Offloader. A intenção da mudança é reduzir CPU desperdiçada enquanto a fila está ociosa. Os dados disponíveis confirmam queda de uso do Offloader, mas ainda não demonstram aumento de FPS nem isolam regressões.

### Diagnóstico RSX opcional

O novo `rpcs3/Emu/RSX/RSXDiagnostics.h` agrega duração de parede, contagens e tempo exclusivo/inclusivo para atrasos de wake-up, sincronização do Offloader, espera de semáforo, submissão Vulkan e fences. Também conta transições e tempo em estados da FIFO. A coleta detalhada exige iniciar um processo novo com `RPCS3_RSX_DIAGNOSTICS=1`; sem o opt-in, os relatórios ficam desligados.

O diagnóstico publica linhas JSON `RSX_DIAG` aproximadamente por segundo ou em eventos de frame. As durações incluem preempção e escalonamento: **não são tempo de CPU ativa nem tempo de GPU ocupada**. Estados FIFO se sobrepõem aos escopos de espera e não podem ser somados entre si. Não há relógio por comando ou por iteração de spin.

Arquivos que integram esse diagnóstico:

- `rpcs3/Emu/RSX/RSXThread.cpp` e `RSXThread.h`: acumuladores, estados, eventos por frame e exportação dos snapshots.
- `rpcs3/Emu/RSX/RSXFIFO.cpp`: transições da FIFO e registro de comando durante captura nativa.
- `rpcs3/Emu/RSX/NV47/HW/nv406e.cpp`: medição do tempo aguardando semáforos RSX.
- `rpcs3/Emu/RSX/VK/vkutils/sync.cpp`: medição de espera para submissão e fences Vulkan.
- `rpcs3/emucore.vcxproj` e `rpcs3/emucore.vcxproj.filters`: inclusão do novo header no projeto Windows.

### Perfil de esperas do caminho de iluminação do jogo

Os novos `rpcs3/Emu/Cell/Infamous2LightWaits.h` e `Infamous2LightProfile.h` agregam tentativas de `usleep` e espera de semáforo feitas por uma thread PPU específica do jogo. `sys_timer.cpp` e `sys_semaphore.cpp` instanciam os escopos. A coleta exige `RPCS3_INFAMOUS2_LIGHT_PROFILE=1`, além da identidade do jogo, hash e dois endereços de syscall esperados.

Os registros `INFAMOUS2_LIGHT_WAITS` agrupam tentativas, soma/máximo do tempo de parede, valor solicitado ao `usleep` e saídas por `cpu_flag::again`, por estado da thread. São tentativas HLE e espera de parede; não medem execução do kernel SPU de iluminação, custo de luz por quadro ou CPU ativa. Uma espera mais longa não identifica sozinha o produtor que atrasou o semáforo.

### Captura nativa e metadados Vulkan

`RPCS3_INFAMOUS2_CAPTURE=1` ativa, somente para a identidade verificada, registros auxiliares ligados a uma captura nativa de um frame. `RSXThread.cpp` e `RSXFIFO.cpp` registram começo/fim, eventos de comando FIFO e lotes lógicos de desenho. `VKDraw.cpp`, `VKGSRender.cpp` e `VKGSRender.h` registram estados selecionados do lote, motivos de skip, metadados de shader/surface/viewport/scissor, texturas e descritores, UBOs e chamadas de desenho Vulkan.

A captura é observacional e tem limites de volume: até 100.000 registros gerais e 16.384 registros auxiliares de backend. Um lote RSX pode ser adiado, ignorado ou combinado pelo backend; um registro de `vkCmdDraw` não prova execução concluída pela GPU. Os metadados não fornecem o PC/LR PPU que produziu o comando nem o tempo de GPU do passe. A captura nativa pausa a emulação e adiciona overhead: não usar seus trechos como benchmark.

Arquivos de integração Vulkan: `rpcs3/Emu/RSX/VK/VKDraw.cpp`, `VKGSRender.cpp` e `VKGSRender.h`.

### Lista dos arquivos alterados

Arquivos existentes modificados:

```text
rpcs3/Emu/Cell/lv2/sys_semaphore.cpp
rpcs3/Emu/Cell/lv2/sys_timer.cpp
rpcs3/Emu/RSX/NV47/HW/nv406e.cpp
rpcs3/Emu/RSX/RSXFIFO.cpp
rpcs3/Emu/RSX/RSXOffload.cpp
rpcs3/Emu/RSX/RSXThread.cpp
rpcs3/Emu/RSX/RSXThread.h
rpcs3/Emu/RSX/VK/VKDraw.cpp
rpcs3/Emu/RSX/VK/VKGSRender.cpp
rpcs3/Emu/RSX/VK/VKGSRender.h
rpcs3/Emu/RSX/VK/vkutils/sync.cpp
rpcs3/emucore.vcxproj
rpcs3/emucore.vcxproj.filters
```

Arquivos novos:

```text
rpcs3/Emu/Cell/Infamous2LightProfile.h
rpcs3/Emu/Cell/Infamous2LightWaits.h
rpcs3/Emu/RSX/RSXDiagnostics.h
```

## Medições e achados existentes

### Comparação do Offloader

Uma comparação anterior registrou o trecho de referência por cerca de 114 s e o modificado por 61 s. As cenas e ações não foram iguais:

| Medida | Referência | Modificado |
|---|---:|---:|
| FPS médio | 35,71 | 34,63 |
| 1% low | 26,11 | 26,24 |
| P99 frametime | 35,21 ms | 35,87 ms |
| CPU do Offloader, um processador lógico | 98,46% | 5,84% |

A redução de CPU do Offloader é clara nesse registro. A comparação não prova ganho ou perda de FPS, pois os trechos diferiram e o 1% low ficou praticamente igual. É necessário repetir com percurso, cena, duração e aquecimento de cache comparáveis.

### A/B controlado do Offloader (Claude, 24/10/2026 12:05–13:16)

Substitui a comparação acima como evidência de FPS. Quatro rodadas alternadas (baseline `D54E12…` / offloader `F26AE…`), mesmo save, config e percurso manual de 90 s, sem variáveis de diagnóstico. Média baseline → offloader: **31,21 → 32,89 FPS (+5,4%)**, 1% low **22,03 → 23,16**, P99 **42,56 → 39,88 ms**. A pior rodada com a mudança ficou acima da melhor sem ela. CPU do Offloader 98% → 5,5%; `rsx::thread` inalterado (~94%); `main_thread` PPU 48% → 38% (causa não verificada; hipótese de disputa SMT). Sem fatais. Detalhes e limites: `C:\Users\T-GAMER\rpcs3-dev-tools\monitoring\AB_OFFLOADER_20261024.md`.

Também nessa sessão:

- O diff foi salvo no **commit local `c5656232c`** (branch `codex/rsx-offloader-idle`, sem push). É exatamente a fonte da build `F26AE…`.
- Backup de saves em `rpcs3-dev-tools\backups\saves-ab-20261024-120353`.
- A sessão ETW `CodexInfamousLive29780_14400` (SPU Profiler das 11:54) ficou órfã e bloqueava novas capturas do PresentMon; foi encerrada. Novas ferramentas: `rpcs3-dev-tools\Start-AB-Run.ps1`, que encerra sessões órfãs e lança uma rodada limpa com coletores, e `Analyze-AB-Run.py`.
- Não vi resultado analisado da sessão do SPU Profiler das 11:54. Confirmar se ela chegou a ser jogada antes de reaproveitar.

### SPU Profiler no mesmo percurso (Claude, 24/10/2026 13:20–13:27)

Run `experiments\infamous2-analysis\profiler-runs\20261024-132057-202-2b9ab9b4`, relatório `RESULTADO_PERCURSO.md`. Diferença entre o flush antes do percurso e o encerramento (~130 s; ~90 s de percurso). Das amostras ativas das 7 threads SPU: **spulight 24,5%**, spumesh 11,9%, spudraw 7,5%, edgepost 6,8%, visprog 5,2%, edgeanim 2,8%; **32,2% sem match nos 27 módulos**, com um único bloco `pNvKT3V-0x04720` (LS 0x44a8, 732 words) = 12,2%. pedspn/move/physclip só em corpos compartilhados. A 2ª instância `CellSpursKernel0` (`0x02000005`) fica ~92% em wait no profiler, mas usa ~88% de um núcleo no host: hipótese de espera ativa (config com `SPU GETLLAR Busy Waiting Percentage: 100`), não verificada no fonte. Overhead do profiler pequeno (32,62 FPS no percurso).

**Identificação do bloco desconhecido (Claude, 24/10):** `pNvKT3V-0x04720` é código do jogo. Está no 5º de **cinco módulos SPU não catalogados** no EBOOT (VA 0x87D500, 0x87D880, 0x87E480, 0x87EC80, 0x87F880, entre `edgepost` e `spumesh`). Eles usam cabeçalho `0xDA7ABA5E` em +0x10 em vez de `C0DEC0DE`, carga em LS 0x4000. São enviados por 0x354180/0x35379C/0x353F38/0x353CF4/0x3539E4 (helper comum 0x35371C), em sequência fixa pelo driver por quadro `0x35451C` (único chamador 0x1EF860). A função vizinha 0x35435C usa código do mesmo bloco de compilação do registro do `edgepost` (0xF49F8). Provável pipeline Edge Post; efeito de cada estágio desconhecido. O bloco é cálculo SIMD sem instruções de canal, não espera. **Os 5 somam 25,1% das amostras SPU ativas**, empatados com o spulight; com `edgepost`, o pós-processamento chega a ~32%. Detalhes: `profiler-runs\20261024-132057-202-2b9ab9b4\IDENTIFICACAO_pNvKT3V.md`.

**Efeito dos 5 estágios (Claude, 24/10; inferência estática, sem confirmação visual):** pipeline de **SSAO em meia resolução**. mod1 = redução 2:1 e linearização da profundidade D24 (alta confiança); mod5 = cálculo do AO a partir da profundidade linear + buffer RGBA8 (provável normais), saída de 8 bits; mod2/mod3 = desfoque horizontal/vertical sensível à profundidade; mod4 = upsample bilateral usando a profundidade cheia. Superfícies no objeto global `0xA3C990`. Hipóteses abertas: com `Write Depth Buffer: false` as SPUs podem ler profundidade/normais desatualizadas; a espera de semáforo do RSX pode incluir aguardar esse SSAO. Detalhes e disassembly: `experiments\infamous2-analysis\static\postfx-stages\ESTAGIOS_RESULTADO.md`. Nova ferramenta: `tools\spu_ls_disasm.exe` (SPUDisAsm do RPCS3).

**Testes em jogo (Claude, 24/10 14:00–14:15):**
1. `Write Color/Depth Buffers` ligados não mudaram a cena estática; FPS 32,31, 1% low 20,71 (pior). Manter desligados.
2. Patch de diagnóstico pulando só o mod5 (`0x354634` → nop): cena quase toda escura, inclusive superfícies sob sol, e o **RSX entrou em timeout esperando os semáforos 0x40300890/0x40300870**. O RSX sincroniza com esse pipeline das SPUs a cada quadro. Patch removido; saves sem dano aparente. Identificação revista: SSAO **ou** máscara de sombra do sol em espaço de tela, ainda em aberto. Não repetir testes que desligam estágios. Ferramentas novas: `rpcs3-dev-tools\Capture-GameWindow.ps1`, `Compare-Screenshots.ps1`; `Start-AB-Run.ps1` aceita `-ConfigOverride`/`-Label`.

**Espera do RSX por endereço de semáforo (Claude, 24/10 14:27):** commit local `bfc5be9fa` adiciona ao `RSX_DIAG` (opt-in) a tabela `sem` por endereço NV406E. Build `rpcs3.exe` = `5D2A5E2A…`; a build anterior `F26AE…` foi preservada como `bin\rpcs3-offloader-diag-F26AE709.exe` (variante `offloader` no `Start-AB-Run.ps1`, que agora também tem `semaddr` e `-RsxDiagnostics`). No percurso de 90 s, **21,5% do tempo do RSX é espera de semáforo, toda em 0x40300870 (18,1%; espera em 99,4% dos quadros, 5,5 ms/quadro) e 0x40300890 (3,4%; 1,0 ms/quadro)**, os mesmos endereços que entraram em timeout sem o mod5. As SPUs estão no caminho crítico do quadro. Overhead do diagnóstico desprezível (32,97 FPS). Relatório: `monitoring\ab-semaddr-run1-20261024-142514\RESULTADO_SEMAFOROS.md`. Próximo sugerido: A/B de `Thread Scheduler Mode`/afinidade P-E e análise do código gerado para o bloco quente do mod5.

**A/B de agendamento (Claude, 24/10 14:34–14:47):** OS (atual) 32,97 FPS / 1% low 22,78; **Alternative Scheduler 32,15 / 16,60** (muitos engasgos); **só núcleos P (afinidade 0xFFF) 30,02 / 20,31**. Manter `Operating System` sem afinidade. A espera em 0x40300870 ficou em ~5,2–5,9 ms por quadro em todos os braços: o atraso é trabalho SPU, não posicionamento de threads. Relatório: `monitoring\AB_SCHEDULER_20261024.md`. Ferramentas: `Finish-AB-Run.ps1` (copia o log e roda as análises); `Start-AB-Run.ps1 -AffinityMask`.

**Código gerado para o bloco quente do mod5 (Claude, 24/10; leitura das regras do `SPULLVMRecompiler.cpp`, estimativa sem disassembly x86):** laço de 558 instruções SPU. Custos: 75 `shufb` com máscaras recarregadas por `lqr` dentro do laço (caminho genérico, ~7–8 operações cada, sem AVX-512) e 83 `fma`/`fm` que no **XFloat Approximate** custam ~5 operações cada (Relaxed: 1). Estimativa: ~1.800 operações/iteração em Approximate contra ~1.450 em Relaxed (−20%). Próximo teste: A/B `SPU XFloat Accuracy: Relaxed` com config clonada `config-tests\xfloat-20261024\relaxed.yml`, checando FPS, espera em 0x40300870 e artefatos visuais. Relatório: `static\postfx-stages\MOD5_CODIGO_GERADO.md`.

**A/B XFloat Relaxed (Claude, 24/10 14:52–14:57):** **36,90 FPS (+11,9%)** contra 32,97; espera em 0x40300870 caiu de 5,50 para 3,73 ms por quadro e em 0x40300890 de 1,03 para 0,30 ms. **Mas deforma a cabeça dos NPCs** (relato do jogador + `crop-npcs.png`): erro de precisão no código SPU de animação/skinning. Não adotar globalmente. Próximo caminho proposto: **precisão seletiva**, Relaxed só no pipeline de 5 estágios (e talvez `spulight`), Approximate no resto. Exige mudança no `SPULLVMRecompiler.cpp`. Relatório: `monitoring\AB_XFLOAT_RELAXED_20261024.md`.

**Pacotes turbo (Claude, 24/10 15:09–15:21), commit local `9024a1dfa`:** precisão seletiva por módulo no SPU LLVM (`RPCS3_INFAMOUS2_RELAXED=postfx,light,edgepost,vis,spudraw`, por conteúdo do programa contra o EBOOT, só BCUS98125 01.04; `spumesh`/`edgeanim` excluídos) + spin do Offloader antes de dormir (`RPCS3_OFFLOAD_SPIN_US=50`) + config `turbo2.yml` (`Driver Wake-Up Delay` 0, `GETLLAR` 50). **Turbo v2: 35,35 FPS (+7,2%), 1% low 23,32, sem deformação de NPC nas capturas.** "Usleep Only" foi testado na v1 e é prejudicial (PPU main 99%, Kickers girando). Relatório: `monitoring\TURBO_20261024.md`.

### Sessão de missão, combate e iluminação

O relatório `SESSAO_TESTE_20261023.md` registra uma missão de 120 s feita com a build anterior `8AB171...` e os coletores ligados. Resultados: 37,32 FPS médios, 25,69 de 1% low, 26,79 ms de frametime médio, P99 de 36,01 ms e P99,9 de 42,51 ms. O perfil de threads registrou a thread RSX em 95,30% de um núcleo, as seis SPU principais entre 82,93–88,22%, PPU principal em 46,67% e Offloader em 5,65%.

Na mesma sessão, os escopos atribuíram 24,47% do wall time exclusivo à espera de semáforo, 4,03% à sincronização do Offloader e 2,59% ao wake delay. O estado 2 do Kicker de iluminação teve 92.658 tentativas de `usleep` de 100 µs, com duração HLE média de 446 µs. Esses dados ajudam a escolher medições seguintes; não identificam automaticamente o gargalo nem equivalem a tempo de CPU/GPU.

O trecho de combate anotado pelo jogador teve 34,15 FPS e 1% low de 23,56 em aproximadamente 81,3 s. Como missão e combate tiveram cenas diferentes, esses valores não isolam o custo de NPCs nem comparam executáveis.

### Recorte de chama e fumaça

O usuário e a captura visual mostraram bordas retas atravessando chama e fumaça enquanto a geometria ao fundo continua. Uma captura RRC completa foi analisada; ela contém 5.476 lotes lógicos e 233.499 registros nativos. O parser recuperou também 902 lotes além do limite do log auxiliar. Isso valida estados de renderização, mas lote lógico não equivale a draw call ou duração de GPU.

Foram encontrados lotes candidatos 5326–5330 com amostragem de uma imagem no armazenamento associado ao depth target 512×288 e instruções de comparação/descarte (`SLE`/`KIL`). A análise dos microcodes descartou WPOS como explicação para os oito casos que pareciam ter critérios de escala divergentes. Ainda falta observar a view host e os parâmetros finais de `scale`, `bias` e `clamp`, e ligar esses lotes ao efeito visível. **A causa do recorte permanece desconhecida; nenhum passe foi removido ou corrigido.**

### Iluminação, NPCs e física no executável

Na análise estática do EBOOT foram identificados a thread PPU `SPU Light Task Kicker`, sua máquina de estados, preparação/envio de jobs e o payload SPU `spulight`. Endereços relevantes: `0x350B90` cria a thread, `0x350864` trata estados, `0x3505AC` prepara jobs, `0x350C7C` e `0x350CA8` são os pontos de `usleep`/semáforo medidos. Os módulos `fog`, `spudraw`, `psysltg`, `psysspark` e `occlusion` também foram catalogados. Isso identifica caminhos candidatos, não o custo de cada família.

Foram relacionados os módulos `pedspn`, `move`, `edgeanim` e `physclip` a caminhos PPU de preparação/envio. Também foi analisado um pack PACK9 de polícia e ligado a tipos como `CNpc`, `CNpcSkill`, `CGroundSkill` e `CMovementSkill`. Isso demonstra conteúdo e algumas chamadas/tipos do jogo, **não** o dispatcher central de IA, pathfinding, frequência de atualização ou número de NPCs vivos. Não se deve reduzir população nem alterar budgets com base nesses nomes.

## Ideias dos chats da seção “Infammous2-rpcs3”

Os chats “Otimizações do RPCS3” e “Projeto RPCS3 Otimizado” levantaram hipóteses. As propostas abaixo **não estão implementadas** salvo quando o estado atual diz o contrário.

1. **Distribuir trabalho considerando P-cores e E-cores.** Medir topologia e threads críticas no i5-14400F; experimentar preferência por P-cores para PPU/RSX que atrasem o quadro, comparar SPU em P e E e reservar E-cores para tarefas auxiliares. Não fixar afinidade por número de núcleo sem medir disputa, migração e tempo por quadro.
2. **Acelerar rotinas SPU relacionadas à iluminação.** O caminho `spulight` está mapeado e há um perfil das esperas do Kicker. O próximo dado deve vir do SPU Profiler, separando tempo de execução do kernel, espera e preparação PPU. Se uma rotina quente for demonstrada, avaliar tradução JIT, sequência SIMD nativa ou melhoria localizada com equivalência funcional.
3. **Estudar precisão XFloat seletiva.** Os chats citaram documentação do RPCS3 sobre ganho com precisão Relaxed e problemas em missões específicas. Tratar como hipótese: determinar quais blocos falham e como opções entram na compilação/cache antes de considerar uma política por rotina.
4. **Reduzir esperas caras sem quebrar sincronização.** Medir esperas ativas, latência de wake-up, semáforos e dependências. A alteração do Offloader reduziu uso de CPU nesse worker; os dados ainda apontam semáforos RSX como parcela relevante de wall time. Não remover nem encurtar esperas sem encontrar o produtor, verificar a ordem dos eventos e repetir estabilidade.
5. **Separar IA, movimento, animação, colisão, partículas e desenho.** NPCs não são uma única carga. Usar amostragem PPU/SPU e ligar blocos quentes a módulos antes de otimizar. Uma mudança no jogo que diminua frequência ou qualidade de NPCs é diferente de otimizar a tradução do emulador e pode alterar gameplay.
6. **Investigar RSX/Vulkan, sombras e oclusão ambiente.** Inspecionar cópias de buffers/texturas, sincronização CPU/GPU, invalidação de cache, preparação de comandos, pipelines/shaders e AO Fields/AO Decals. Cache pode reduzir travamentos da primeira compilação sem aumentar FPS sustentado; medir os dois efeitos separadamente.
7. **Considerar compute na RX 9060 XT para um kernel SPU.** É uma ideia avançada para trabalho grande e independente. Só comparar com uma implementação CPU primeiro; transferência, submissão e sincronização podem custar mais do que o cálculo.

Também foi discutido um patch de iluminação de *inFAMOUS 1* que substitui duas instruções por NOP. Seus endereços não se aplicam ao segundo jogo. Não copiar NOPs: primeiro provar a função equivalente em `BCUS98125` 01.04, preservar efeitos e áudio, restringir eventual patch ao hash e validar em cena equivalente.

## Ferramentas e relatórios fora do Git

Os scripts e resultados de análise ficam em `C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis` e `...\monitoring`. Não são commits do repositório. As pastas incluem scripts locais para iniciar a análise, fazer backup dos saves, analisar capturas RSX, decodificar microcodes, examinar o profiler SPU e rastrear dados de partículas/NPCs, junto com logs, JSONs e fixtures.

Relatórios principais para consultar antes de continuar:

```text
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\SESSAO_TESTE_20261023.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\RECORTE_CAPTURA1_RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\PROFILERS_INTERNOS_PLANO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\NPC_PACK_RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\spu-light-RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\NPC_RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\FISICA_RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\static\ISO_NPC_RESULTADO.md
C:\Users\T-GAMER\rpcs3-dev-tools\experiments\PLANO_PATCH_INFAMOUS2.md
```

`PROFILERS_INTERNOS_PLANO.md` recomenda começar com o SPU Profiler, mantendo SPU Debug desligado. O cache SPU atual não precisa ser apagado para essa medição. Uma passagem PPU exige cuidado diferente: a flag `PPU Profiler` não faz parte da chave dos objetos persistentes; preservar os objetos `v*.obj`/`v*.obj.gz` do cache do EBOOT antes de gerar perfil instrumentado e depois restaurar os originais. Não apagar o cache todo.

## Próximos passos sugeridos

1. Começar com inspeção somente leitura do `git status`, diff e `latest-build.json`. Manter as mudanças locais; não usar reset, checkout destrutivo ou push.
2. Antes de qualquer nova partida, confirmar a versão/hash carregados e fazer backup verificável dos saves. Separar teste com profiler/captura de qualquer benchmark de desempenho.
3. Para investigar NPCs/movimento/física, executar uma coleta dedicada do SPU Profiler com SPU Debug desligado, em fases comparáveis, e correlacionar blocos amostrados com os módulos mapeados. Controles e saves devem permanecer iguais; anotar missões e ações.
4. Para o recorte, a próxima observação útil é a view/imagem host e os parâmetros finais de textura (`scale`, `bias`, `clamp`) nos lotes candidatos, ligados visualmente à chama/fumaça. Não alterar WPOS, resolução, efeito ou passe antes desse vínculo.
5. Só propor uma mudança funcional quando um alvo quente ou a causa gráfica estiver demonstrado. Fazer uma alteração por vez, build Release, A/B em percurso equivalente e verificação de estabilidade, missões, áudio, efeitos e saves.
6. Avaliar afinidade P/E e XFloat depois das medições básicas. Nunca interpretar uma queda de uso de um worker como ganho de FPS por si só.

## Regras de colaboração para Claude e Codex

- Tratar este documento como fotografia do estado em 24/10/2026; conferir o checkout e os relatórios antes de assumir que algo continua igual.
- Preservar todas as alterações locais existentes e indicar claramente o que é fonte nova, hipótese, resultado medido ou recomendação.
- Não incluir EBOOT/ELF do jogo, ISO/PSARC, firmware, saves ou dados proprietários em commits ou uploads.
- Não usar dados de wall time como uso de CPU/GPU. Não misturar captura pausada com benchmark; não comparar cenas diferentes como A/B causal.
- Trabalhar uma hipótese por vez, priorizando fidelidade de gameplay e reversibilidade. Anotar hashes/versão do jogo, configuração, build e condições do teste.
- Publicação, push e patches funcionais no jogo continuam fora do escopo até pedido explícito e evidência suficiente.

**Turbo v3 (Claude, 24/10 15:34), commit local `8e5e7a74c`, exe `A2CCEA61…`:** + afinidade híbrida opt-in (`RPCS3_HYBRID_AFFINITY=1` com scheduler "RPCS3 Scheduler": RSX com núcleo P exclusivo 0xC00, resto 0xF3FF) + grupos `mesh`/`fx` na precisão seletiva. **36,65 FPS (+11,2%), 1% low 24,31, P99 36,9 ms, sem deformação percebida.** O trabalho próprio do RSX caiu 1,2 ms por quadro. O `edgeanim` é o suspeito da deformação do Relaxed global. Atalho de uso: `rpcs3-dev-tools\Jogar-Infamous2-Turbo.ps1`. Detalhes: `monitoring\TURBO_20261024.md`.

**Confirmação v3 e v4 (Claude, 24/10 15:55):** v3 repetida deu 36,47 FPS → média **36,56 (+10,9%)**, 1% low 24,51. v4 (v3 + `Accurate RSX reservation access: false`) = 36,63: sem ganho mensurável; manter a opção ligada. Hoje o quadro do RSX é ~21,7 ms de trabalho próprio + ~5,2 ms esperando as SPUs. Próximos alvos: perfil de CPU do `rsx::thread` e constantes de máscara `shufb` nos módulos do jogo no recompilador SPU.

**Leitura do relatório do Codex (Claude, 24/10, após 16:22):** lido `RELATORIO_CODIGO_JOGO_PARA_CLAUDE.md` e a pasta `game-code-audit-20261024-162235`. Pontos que mudam o plano:
- **O patch "Disable Mesh Trimming" (Yahfz, `be32 0x008875c0 → 0x4020007f`, no `spumesh`) está instalado e ativo** em `bin\portable\patches\imported_patch.yml` + `bin\portable\config\patch_config.yml` desde 16:14 (manifesto em `backups\mesh-trimming-patch-20261024-161424`). Todas as medições de Claude até 15:55 são **sem** esse patch. A próxima sessão precisa de nova referência: base + patch e turbo v3 + patch, no mesmo percurso, com captura visual.
- A precisão seletiva compara o programa SPU com a memória do EBOOT já patchada, então continua reconhecendo o `spumesh`. Programas antigos do cache SPU (sem patch) só deixam de casar e ficam em Approximate; não há risco de Relaxed indevido.
- A especialização do mod5 seguirá a proposta do Codex: **constantes verificadas em runtime com fallback genérico**, sem assumir LS imutável; os 5 `lqr` não são todos máscaras (há floats, offsets e zeros).
- Medições do pipeline devem cobrir os dois call sites de `0x35379C` (`0x354634` e `0x354C58`).
