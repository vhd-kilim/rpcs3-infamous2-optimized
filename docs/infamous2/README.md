# RPCS3 dedicado ao inFAMOUS 2

Alvo estudado: BCUS98125, update 01.04, PPU `41a7d047c573c78c0307506c9d1b52d15a66e4f5`.

## Estado

- Espera ociosa do Offloader: A/B alternado de quatro rodadas registrou +5,4% de FPS medio no percurso desta maquina.
- Turbo v3: precisao SPU seletiva, afinidade hibrida e ajustes de espera; duas rodadas registraram 36,56 FPS medios contra 32,97 da referencia com Offloader (+10,9%). Valores especificos do percurso e hardware, sem garantia para outras cenas.
- Patch Disable Mesh Trimming: importado do perfil original; modelos corrigidos segundo o jogador na rodada com o patch.
- Espera de flush por notificacao: reduz CPU, mas piorou FPS na primeira comparacao; opt-in de pesquisa, **nao recomendada como ganho**.
- Busca direta de uma unica view Vulkan: primeira rodada registrou 36,25 FPS contra 35,77 da v3 com patch/profilers (+1,34% observado); repetir antes de afirmar ganho sustentado.

## Opcoes experimentais

| Variavel | Valor do pacote testado | Escopo |
|---|---|---|
| RPCS3_INFAMOUS2_RELAXED | postfx,light,edgepost,vis,spudraw,mesh,fx | BCUS98125 01.04, comparacao de conteudo com o EBOOT |
| RPCS3_OFFLOAD_SPIN_US | 50 | Giro limitado antes de dormir no Offloader; padrao 0 |
| RPCS3_HYBRID_AFFINITY | 1 | Windows hibrido; scheduler nao-OS; depende da topologia |
| RPCS3_RSX_DIAGNOSTICS | 1 apenas em diagnostico | Tempos de parede e esperas, nao CPU ativa |
| RPCS3_INFAMOUS2_RSX_FLUSH_WAIT | **desligada** | Pesquisa de latencia de conclusao Vulkan |

No preset v3 estudado: Driver Wake-Up Delay 0, GETLLAR busy waiting 50, Sleep Timers Accuracy As Host, SPU XFloat global Approximate, reservas precisas ligadas. O teste somente com nucleos P e o Alternative Scheduler pioraram o resultado; a politica hibrida especifica e um experimento diferente.

Nao ativar Relaxed global: houve deformacao de NPCs. Os grupos `anim` e `sim` nao fazem parte do preset. Capturas do percurso nao validam toda a campanha, agua, sombras, fisica ou todos os efeitos.

## Leitura

- [Resultados e perfil do RSX](RSX_PROFILE_20261024.md)
- [A/B do Offloader](measurements/AB_OFFLOADER_20261024.md)
- [Pacotes Turbo](measurements/TURBO_20261024.md)
- [Agendamento](measurements/AB_SCHEDULER_20261024.md)
- [Precisao global e deformacao](measurements/AB_XFLOAT_RELAXED_20261024.md)
- [Leitura do codigo do jogo](../../RELATORIO_CODIGO_JOGO_PARA_CLAUDE.md)
- [Contexto historico Codex/Claude](../../CONTEXTO_COMPARTILHADO_CODEX_CLAUDE_RPCS3_INFAMOUS2.md)
- [Ferramentas](../../tools/infamous2/README.md)

O contexto historico contem fotografias de etapas anteriores; os resultados datados mais recentes e o codigo prevalecem. Nao ha port nativo nem patch funcional de IA/iluminacao/fisica entregue.
