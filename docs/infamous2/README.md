# Documentação técnica — inFAMOUS 2

[Página principal](../../README.md) · [Histórico](CHANGELOG.md) · [Ferramentas](../../tools/infamous2/README.md)

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

## Resultados e evidências

| Documento | Conteúdo |
|---|---|
| [Perfil RSX mais recente](RSX_PROFILE_20261024.md) | WPR/PDB, esperas, notificação e busca de views |
| [Resumo estruturado RSX](measurements/RSX_AB_20261024.json) | Três variantes com métricas e identificação das builds |
| [A/B do Offloader](measurements/AB_OFFLOADER_20261024.md) | Quatro rodadas alternadas |
| [Pacotes Turbo](measurements/TURBO_20261024.md) | Evolução v1–v4 e duas rodadas v3 |
| [Agendamento](measurements/AB_SCHEDULER_20261024.md) | Alternativas de scheduler e afinidade |
| [Precisão global e deformação](measurements/AB_XFLOAT_RELAXED_20261024.md) | Problema visual que descartou Relaxed global |
| [Ferramentas](../../tools/infamous2/README.md) | Coleta, análise, perfil offline, backup e patch |

## Registros de pesquisa e colaboração

- [Leitura do código do jogo](../../RELATORIO_CODIGO_JOGO_PARA_CLAUDE.md): mapa estático e candidatos; não equivale a código-fonte original ou patch de IA validado.
- [Contexto histórico Codex/Claude](../../CONTEXTO_COMPARTILHADO_CODEX_CLAUDE_RPCS3_INFAMOUS2.md): etapas e caminhos do laboratório. Pode conter planos e estados anteriores.
- [Histórico resumido](CHANGELOG.md): alterações integradas e questões abertas.

Para o estado atual, use este índice, os relatórios datados e o código correspondente. ISO, EBOOT/ELF, firmware, saves, PDBs e ETLs completos permanecem fora do repositório.
