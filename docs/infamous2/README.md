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
| Envio antecipado Vulkan v2 | 39,07 contra 37,10 FPS (+5,3%), mesmo executável, A/B alternado sem RSX_DIAG |
| LSCONST restrita | v1: 40,68 contra 39,35 FPS (+3,4%); v2 com regra de ambiguidade repetiu 40,68 no grupo restrito |
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
| `RPCS3_INFAMOUS2_EARLY_SUBMIT` | `256` no A/B v2; padrão `0` | Envio antes de espera NV406E real e em troca de render target após o limite de draws |
| `RPCS3_INFAMOUS2_LSCONST` | `light,edgepost,postfx`; padrão vazio | LQR especializado, com comparação de tabelas na entrada; ainda sem fallback |
| `RPCS3_INFAMOUS2_FB_PREFETCH` | **desligada** | Experimento redundante na cena medida, sem cópias adicionais |

Configuração v3 estudada:

| Opção RPCS3 | Valor |
|---|---|
| Thread Scheduler Mode | RPCS3 Scheduler, junto da flag de afinidade híbrida |
| Driver Wake-Up Delay | 0 |
| SPU GETLLAR Busy Waiting | 50 |
| Sleep Timers Accuracy | As Host |
| SPU XFloat global | Approximate |
| Accurate RSX reservation access | true |
| RSX FIFO Fetch Accuracy | Atomic; Fast não trouxe ganho convincente |

O teste somente com núcleos P e o Alternative Scheduler pioraram o resultado; a política híbrida específica é um experimento diferente. **Não ativar Relaxed global:** houve deformação de NPCs. Os grupos `anim` e `sim` ficam fora do preset seletivo. O percurso não valida toda a campanha, água, sombras, física ou todos os efeitos.

O [lançador versionado](../../tools/infamous2/Jogar-Infamous2-Otimizado.ps1) é um snapshot da v3 + busca de views, sem coletores e com backup de saves. Ele não ativa EARLY_SUBMIT/LSCONST e contém caminhos e hash locais: não é instalador portátil nem substitui a compilação.

## Limites da especialização LSCONST

Não confundir `RPCS3_INFAMOUS2_LSCONST` com `RPCS3_INFAMOUS2_RELAXED`: são otimizações independentes. Para LSCONST, os testes indicam apenas `light,edgepost,postfx`. A versão com filtro de ambiguidade exige pelo menos 64 palavras não nulas de código e compara as tabelas entre módulos catalogados compatíveis.

- Adicionar `fx,spudraw` gerou divergências em runtime; uma tentativa anterior também causou crash. Não usar esses grupos nessa opção.
- Adicionar `mesh,vis` teve 40,14 FPS contra 40,68 do grupo restrito; não demonstrou ganho.
- Uma divergência detectada na entrada **só é registrada**; a função ainda segue com os valores compilados. Também falta garantir que a tabela não seja modificada durante a função.
- O grupo restrito não divergiu nos percursos registrados, mas isso não prova segurança em toda a campanha. A opção permanece desligada por padrão.

## Resultados recentes

Os [dados estruturados](measurements/AB_20261024.json) publicam apenas as métricas resumidas dos testes, com hashes e condições. As conversas e os relatórios internos continuam locais.

| Série | Referência: FPS / 1% low | Candidato: FPS / 1% low | Decisão |
|---|---:|---:|---|
| Envio antecipado v2 | 37,10 / 25,96 | 39,07 / 26,06 | Ganho de média, sem mudança relevante do 1% low |
| LSCONST v1 restrita | 39,35 / 26,83 | 40,68 / 27,11 | Ganho observado; ainda experimental |
| LSCONST v2: restrita → +mesh,vis | 40,68 / 27,07 | 40,14 / 26,58 | Não ampliar os grupos |

As rodadas foram percursos manuais de 90 s, com 87 s analisados e RSX_DIAG desligado. Cada série tem duas rodadas por braço. Não são medições da campanha inteira nem uma única série cumulativa; não somar ganhos ou comparar diretamente com testes sob WPR/RSX_DIAG.

O diagnóstico atribuiu o escritor de `0x40300870` a `spulight` e o de `0x40300890` a `edgepost` nos caminhos instrumentados. Isso localiza a sincronização, mas não mede sozinho o tempo de CPU de cada job. O detector de recargas por listas DMA só reconhece mudanças nos primeiros 64 bytes; recargas iguais podem escapar, limitando as durações atribuídas.

## Condições da comparação RSX

O [resumo estruturado](measurements/RSX_AB_20261024.json) contém métricas e identificação das três builds: v3 com patch, espera por notificação e busca de view única. Foram percursos manuais de 90 segundos, com 87 segundos analisados após descarte inicial de 3 segundos. PresentMon, diagnóstico RSX e WPR estavam ativos nos três testes; a espera por notificação ficou desligada na variante de busca de views.

| Variante | FPS médio | 1% low | P99 |
|---|---:|---:|---:|
| v3 com patch | 35,77 | 23,71 | 38,71 ms |
| Notificação de flush | 34,71 | 23,02 | 39,26 ms |
| Busca de view única | 36,25 | 24,04 | 37,85 ms |

Uma rodada por variante não separa ganho pequeno de variação de cena. O início por mensagem tem imprecisão; o cache e a presença de profilers também afetam a comparação com outras séries. O 1% low é 1000 dividido pela média do 1% de intervalos mais lentos. Amostras nativas de CPU por função não equivalem à duração de cada chamada.

Builds Windows Release x64 e testes específicos do helper de espera foram executados. O log da build LSCONST v2 registra zero erros e três avisos, incluindo dependência ausente da suíte GoogleTest. A suíte geral, outras máquinas e uma campanha completa não foram validadas.

Consulte as [ferramentas](../../tools/infamous2/README.md) para coleta, análise, backup e patch. Conversas de assistentes e relatórios internos de trabalho ficam locais, fora da versão publicada. ISO, EBOOT/ELF, firmware, saves, PDBs e ETLs completos também não são distribuídos.
