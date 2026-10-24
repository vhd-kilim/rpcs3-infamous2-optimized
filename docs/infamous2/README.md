# Documentação técnica — inFAMOUS 2

[Página principal](../../README.md) · [Ferramentas](../../tools/infamous2/README.md)

Alvo validado: **BCUS98125, atualização 01.04**, PPU `41a7d047c573c78c0307506c9d1b52d15a66e4f5`. Revisão: **24/10/2026**.

## Estado atual

| Componente | Evidência e decisão |
|---|---|
| Espera ociosa do Offloader | Quatro percursos A/B alternados: +5,4% de FPS médio nesta máquina |
| Turbo v3 | Duas rodadas: 36,56 FPS contra 32,97 da referência com Offloader (+10,9%); resultado do pacote |
| Disable Mesh Trimming | Patch de Yahfz; modelos corrigidos segundo o jogador durante a rodada com o patch |
| Busca direta de view única | 36,25 contra 35,77 FPS da v3 com patch/profilers (+1,34% observado); repetir antes de afirmar ganho sustentado |
| Espera de flush por notificação | Menor CPU e pior FPS no primeiro par; desligada por padrão |
| Fumaça/chama recortadas | Problema ainda aberto; patch de modelos não comprova correção dos efeitos |
| IA, física e iluminação | Investigação em andamento; sem patch específico funcional publicado |

Testes em i5-14400F/RX 9060 XT, com percursos manuais. Séries diferentes não são uma comparação cumulativa. Correlação com NPCs ou efeitos não comprova a causa do gargalo.

## Opções experimentais

| Variável | Valor do pacote testado | Escopo |
|---|---|---|
| `RPCS3_INFAMOUS2_RELAXED` | `postfx,light,edgepost,vis,spudraw,mesh,fx` | BCUS98125 01.04, comparação de conteúdo com o EBOOT |
| `RPCS3_OFFLOAD_SPIN_US` | `50` | Giro limitado antes de dormir no Offloader; padrão `0` |
| `RPCS3_HYBRID_AFFINITY` | `1` | Windows híbrido, com RPCS3 Scheduler; depende da topologia |
| `RPCS3_RSX_DIAGNOSTICS` | `1` somente em diagnóstico | Tempos de parede e esperas, não CPU ativa |
| `RPCS3_INFAMOUS2_RSX_FLUSH_WAIT` | **desligada** (`0`) | Pesquisa de latência de conclusão Vulkan |

Configuração v3 estudada:

| Opção RPCS3 | Valor |
|---|---|
| Thread Scheduler Mode | RPCS3 Scheduler, junto da flag de afinidade híbrida |
| Driver Wake-Up Delay | 0 |
| SPU GETLLAR Busy Waiting | 50 |
| Sleep Timers Accuracy | As Host |
| SPU XFloat global | Approximate |
| Accurate RSX reservation access | true |

O teste somente com núcleos P e o Alternative Scheduler pioraram o resultado; a política híbrida específica é um experimento diferente. **Não ativar Relaxed global:** houve deformação de NPCs. Os grupos `anim` e `sim` ficam fora do preset seletivo. O percurso não valida toda a campanha, água, sombras, física ou todos os efeitos.

O [lançador local](../../tools/infamous2/Jogar-Infamous2-Otimizado.ps1) registra a última build testada (v3 + busca de views), sem coletores e com backup de saves. Contém caminhos e hash locais: não é instalador portátil nem substitui a compilação.

## Condições da comparação RSX

O [resumo estruturado](measurements/RSX_AB_20261024.json) contém métricas e identificação das três builds: v3 com patch, espera por notificação e busca de view única. Foram percursos manuais de 90 segundos, com 87 segundos analisados após descarte inicial de 3 segundos. PresentMon, diagnóstico RSX e WPR estavam ativos nos três testes; a espera por notificação ficou desligada na variante de busca de views.

| Variante | FPS médio | 1% low | P99 |
|---|---:|---:|---:|
| v3 com patch | 35,77 | 23,71 | 38,71 ms |
| Notificação de flush | 34,71 | 23,02 | 39,26 ms |
| Busca de view única | 36,25 | 24,04 | 37,85 ms |

Uma rodada por variante não separa ganho pequeno de variação de cena. O início por mensagem tem imprecisão; o cache e a presença de profilers também afetam a comparação com outras séries. O 1% low é 1000 dividido pela média do 1% de intervalos mais lentos. Amostras nativas de CPU por função não equivalem à duração de cada chamada.

Builds Windows Release x64 e testes específicos do helper de espera foram executados. A suíte geral GoogleTest, outras máquinas e uma campanha completa não foram validadas.

Consulte as [ferramentas](../../tools/infamous2/README.md) para coleta, análise, backup e patch. Conversas de assistentes e relatórios internos de trabalho ficam locais, fora da versão publicada. ISO, EBOOT/ELF, firmware, saves, PDBs e ETLs completos também não são distribuídos.
