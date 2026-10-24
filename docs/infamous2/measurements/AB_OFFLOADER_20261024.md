# A/B controlado do RSX Offloader — 24/10/2026

**Pergunta:** a espera ociosa do Offloader (`thread_ctrl::wait_on` no lugar de `yield()`) muda o desempenho do inFAMOUS 2?

**Resposta:** sim, um ganho pequeno e consistente: **+1,68 FPS médios (+5,4%)**, 1% low **+5,1%** e P99 **−2,7 ms**. Nas duas rodadas, a pior rodada com a mudança ficou acima da melhor rodada sem ela.

## Condições

- Jogo: BCUS98125 01.04, mesmo save, mesma config (`config_BCUS98125.yml`: Vulkan, escala 200%, Multithreaded RSX ligado, Driver Wake-Up Delay 200, sem limite de FPS, VSync desligado).
- Builds:
  - **baseline:** `rpcs3-baseline-0dec46b1.exe`, SHA-256 `D54E1221…52A7`.
  - **offloader:** `rpcs3.exe`, SHA-256 `F26AE709…EAA5`, fonte = commit local `c5656232c`.
- Nenhuma variável de diagnóstico ativa (`RPCS3_RSX_DIAGNOSTICS`, `RPCS3_INFAMOUS2_*` removidas do processo). Os hooks compilados ficam inativos.
- Ordem alternada: baseline → offloader → baseline → offloader. O jogador fez o mesmo percurso de 90 s pela cidade depois de aquecer o cache.
- Janela analisada: 90 s a partir da mensagem "comecei", descontando os 3 s iniciais pelo atraso do chat (~87 s de frames por rodada).
- Frametimes vêm do PresentMon 2.3.1, intervalo entre apresentações; o 1% low é 1000 dividido pela média do 1% de intervalos mais lentos. A CPU por thread vem do `GetThreadTimes` a cada 2 s, e 100% equivale a um núcleo lógico.
- Backup dos saves verificado antes: `backups\saves-ab-20261024-120353` (12 arquivos).

## Resultados

| Rodada | Build | FPS médio | 1% low | P50 ms | P99 ms | P99,9 ms | Máx ms | >50 ms | CPU Offloader | CPU rsx::thread | CPU main_thread |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | baseline | 31,22 | 22,90 | 31,73 | 42,31 | 45,58 | 47,68 | 0 | 98,3% | 94,5% | 48,5% |
| 2 | offloader | 33,02 | 23,74 | 29,89 | 39,22 | 48,06 | 54,19 | 1 | 5,5% | 94,5% | 38,1% |
| 3 | baseline | 31,21 | 21,16 | 31,79 | 42,82 | 57,12 | 64,27 | 5 | — | — | — |
| 4 | offloader | 32,75 | 22,58 | 30,18 | 40,55 | 49,04 | 60,32 | 2 | 5,6% | 94,2% | 37,6% |

| Média | FPS | 1% low | P50 ms | P99 ms | P99,9 ms |
|---|---:|---:|---:|---:|---:|
| baseline | 31,21 | 22,03 | 31,76 | 42,56 | 51,35 |
| offloader | **32,89** | **23,16** | **30,04** | **39,88** | **48,55** |
| diferença | +1,68 (+5,4%) | +1,13 (+5,1%) | −1,72 | −2,68 | −2,80 |

Estabilidade: 0 fatais, sem `Access violation` ou `DEVICE_LOST` em nenhuma rodada; contagens de erros e avisos do log semelhantes entre as builds (E 132–193, W ~27,5 mil).

## Leitura

- A variação entre rodadas da mesma build foi pequena (FPS baseline 31,22/31,21; offloader 33,02/32,75), menor que a diferença entre builds. Com n = 2 por variante não há teste estatístico formal; a conclusão vale para esta cena e esta máquina.
- O `rsx::thread` continua em ~94% de um núcleo nas duas builds. A mudança não alivia o RSX.
- A `main_thread` PPU caiu de ~48% para ~38%. **Hipótese, não verificada:** o Offloader girando a 98% disputava um núcleo físico (SMT) ou causava migração com threads críticas no i5-14400F. Liberá-lo reduz essa disputa. Testar com afinidade P/E ou com o diagnóstico `wake_delay`/`offloader_sync` antes de afirmar.
- Os picos isolados (máx. e P99,9) variam muito entre rodadas da mesma build; não dá para tirar conclusão deles.

## Limites

- O início de cada janela tem alguns segundos de imprecisão (mensagem no chat). O percurso é manual: a rota se repete, mas não quadro a quadro.
- Rodada 3: o perfil de threads tinha limite de 40 min e parou antes do percurso (sessão aberta por ~47 min). Os frametimes estão completos. O limite do lançador foi aumentado para 4 h.
- Rodada 1: a primeira tentativa de percurso foi descartada porque uma sessão ETW órfã da sessão do SPU Profiler (11:54) bloqueava o PresentMon. Ela foi encerrada e o percurso refeito. O `Start-AB-Run.ps1` agora encerra sessões órfãs.
- Rodada 2: uma tentativa foi descartada porque o jogador errou o percurso. A janela válida é a segunda.
- `msGPUActive` saiu 0 na rodada 1 (provável efeito da sessão ETW reiniciada) e ~10 ms nas demais.

## Arquivos

- Rodadas: `monitoring\ab-{baseline,offloader}-run{1..4}-*\` (`session.json` com janelas, `ab-summary.json`, CSV do PresentMon, `threads.json`, `samples.csv`).
- Ferramentas: `Start-AB-Run.ps1` (lança rodada limpa e coletores), `Analyze-AB-Run.py` (recorta e resume a janela).
