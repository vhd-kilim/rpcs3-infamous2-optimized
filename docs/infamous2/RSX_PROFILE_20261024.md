# Perfil de CPU do RSX e resultados de 24/10/2026

## Condicoes

BCUS98125 01.04, Vulkan, escala 200%, i5-14400F, RX 9060 XT. Mesma configuracao Turbo v3 e patch **Disable Mesh Trimming**, de Yahfz, nas duas rodadas. O jogador confirmou que o patch corrigiu os defeitos de modelos de Cole/NPCs neste percurso.

Percurso manual de 90 s; janela de frametimes de 87 s apos descartar os 3 s iniciais pelo atraso da mensagem. PresentMon, diagnostico RSX e WPR CPU ativos nos dois bracos. Inicio aproximado por mensagem do jogador. Uma rodada por variante: o resultado e indicativo, nao uma estimativa estatistica definitiva.

| Medida | v3 + patch | Experimento de notificacao |
|---|---:|---:|
| SHA256 inicial | A2CCEA61... | 898B32E7... |
| Inicio, America/Sao_Paulo | 16:54:02 | 17:17:41 |
| FPS medio | **35,77** | 34,71 |
| 1% low | **23,71** | 23,02 |
| P99 | **38,71 ms** | 39,26 ms |
| Intervalos >50 ms | 1 | 2 |
| CPU RSX, percentual de um processador logico | 92,79% | 88,57% |
| Espera 0x40300870 por flip | 4,790 ms | 5,402 ms |
| Espera 0x40300890 por flip | 0,408 ms | 0,565 ms |

O 1% low e 1000 dividido pela media do 1% de intervalos mais lentos. A comparacao com a v3 antiga sem o patch (36,56 FPS em duas rodadas) nao isola o custo do patch: profiler e condicoes de jogo tambem diferem.

## Perfil nativo: enderecos amostrados, nao duracao de chamadas

ETL do Windows filtrado para a thread RSX durante a mesma janela. O leitor offline usa OpenTrace/ProcessTrace e DbgHelp, sem abrir ou suspender o processo do jogo. O formato segue `SampledProfileTraceData`/`ImageLoadTraceData` do projeto Microsoft PerfView. Simbolos privados PDB compativeis; nenhum evento perdido reportado no cabecalho ETL. Amostras DPC/ISR excluidas.

| Ponto amostrado | v3: % das 81.219 amostras | Experimento: % das 77.007 amostras |
|---|---:|---:|
| nv406e::semaphore_acquire | 19,76% | 22,88% |
| VKGSRender::do_local_task | **5,66%** | **0,11%** |
| FIFO_control::fetch_u32 | 4,33% | 4,43% |
| rsx::thread::end | 3,49% | 3,58% |
| rsx::thread::run_FIFO | 3,34% | 3,35% |
| vk::viewable_image::get_view | 1,70% | 1,63% |
| Driver AMD Vulkan, sem simbolos privados | 7,00% | 6,78% |

Na v3, 4.523 das 4.597 amostras de `do_local_task` apontam a linha da espera `m_flush_requests.consumer_wait()`. O restante nao deve ser chamado de trabalho util do RSX: ha esperas, codigo do driver, kernel e aproximadamente 14% de enderecos fora das imagens de usuario catalogadas. A tabela e de amostras **self**, nao uma arvore de chamadas inclusiva; inlining e a granularidade do PDB limitam atribuicao por linha.

O PerfView foi obtido da distribuicao oficial Microsoft, mas sua exportacao foi bloqueada pela revisao automatica do ambiente. A analise utilizou o leitor offline incluido em `tools/infamous2/Extract-RsxCpu.cpp`.

## Experimento 1: espera por conclusao de flush

Flag: `RPCS3_INFAMOUS2_RSX_FLUSH_WAIT=1`, restrita a Title ID, versao e hash PPU do jogo. Desligada por padrao.

Depois de servir um pedido, o RSX conserva o mutex da fila e aguarda o mesmo contador de produtores chegar a zero. O experimento faz 64 giros curtos e entao usa espera atomica. O ultimo produtor notifica os consumidores. A condicao e reavaliada depois de acordar; o caso de conclusao entre leitura e estacionamento nao perde a notificacao.

O teste de concorrencia passou: conclusao imediata, handoff curto, conclusao antes de estacionar e 200 rodadas com oito produtores. Ele valida o helper com atomicas C++20; nao substitui teste do runtime/Vulkan.

**Decisao:** nao promover como otimizacao de FPS. O giro caiu e o uso de CPU diminuiu, mas o FPS observado caiu cerca de 3%. A latencia de estacionamento/despertar e uma explicacao possivel, ainda nao isolada. O caminho continua opt-in para pesquisa e nao deve entrar no preset recomendado.

## Experimento 2: busca de view unica

O perfil identifica `viewable_image::get_view` como custo separado. Foi acrescentado um caminho direto quando o mapa possui exatamente uma entrada: comparar sua chave completa, que inclui remap e aspecto. Mapas com zero ou varias entradas mantem a busca original.

Nao existe ponteiro persistente adicional: clear, descarte, clone e recriacao continuam usando o mesmo container. Uma chave diferente em mapa unitario produz miss exatamente como `find`.

Estado antes da rodada final: candidato compilado para validacao. O resultado dessa rodada esta abaixo; nao confundir o teste da espera atomica com o teste da busca de views.

## Rodada adicional: busca direta de view unica

Build SHA256 `532E806D413727DB05BC24D648A5B75D2DA15D9B51D2F195B964B56AFE1D9DCA`, inicio 17:28:36 (America/Sao_Paulo), mesma configuracao/patch/profilers; espera por notificacao explicitamente desligada.

| Medida | v3 + patch | Busca direta |
|---|---:|---:|
| FPS medio | 35,77 | **36,25** |
| 1% low | 23,71 | **24,04** |
| P99 | 38,71 ms | **37,85 ms** |
| Intervalos >50 ms | 1 | 1 |
| CPU RSX, um processador logico | 92,79% | 92,91% |
| get_view: amostras self | 1.379 / 81.219 (1,70%) | 1.115 / 81.302 (1,37%) |

Ganho observado de 1,34% de FPS e 1,39% no 1% low; a amostra e uma rodada por variante e nao isola variacao de cena. O resultado e compativel com uma melhoria pequena no lookup, mas **nao confirma ganho sustentado na campanha**. O par nao e perfeitamente isolado no binario: o codigo opt-in da espera tambem esta presente, embora desligado, e o compilador reorganizou algumas funcoes.

Nota de atribuicao importante: nesta ultima build o compilador separou `consumer_wait` de `do_local_task`. A espera antiga reaparece com 4,97% em `consumer_wait`, mais 0,12% em `do_local_task`. Isso nao significa que desativar a notificacao eliminou a espera; comparar somente o nome do caller seria enganoso. Na build com notificacao eram 0,02% + 0,11%, respectivamente.

WPR: 81.302 amostras selecionadas, 147 amostras DPC/ISR excluidas, zero eventos perdidos reportados. [Resumo estruturado dos tres bracos](measurements/RSX_AB_20261024.json).

## Material publicado e material local

As tabelas, scripts e fontes de medicao podem ser versionados. ETLs completos, PDBs, saves, EBOOT/ELF, ISO, packs e dumps do codigo do jogo ficam locais. Os rastros completos sao grandes e devem ser compactados/verificados antes de eventual remocao dos originais.
