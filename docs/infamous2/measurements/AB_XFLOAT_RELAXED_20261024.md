# A/B `SPU XFloat Accuracy`: Approximate × Relaxed — 24/10/2026

**Resultado:** o Relaxed deu **+12% de FPS** e cortou a espera do RSX pelas SPUs em ~1/3, mas **deforma a cabeça dos NPCs**: a cabeça entra no corpo ou fica de lado, principalmente em NPCs distantes ou no canto da tela. Isso foi relatado pelo jogador e aparece na captura F12. **Não adotar como config global.**

| Métrica | Approximate (atual) | Relaxed | Diferença |
|---|---:|---:|---:|
| FPS médio | 32,97 | **36,90** | **+11,9%** |
| 1% low | 22,78 | 23,13 | +1,5% |
| P99 | 39,9 ms | 37,9 ms | −2,0 ms |
| Quadros > 50 ms | 3 | 2 | |
| Espera de semáforo (% do tempo do RSX) | 21,5% | **14,8%** | |
| 0x40300870 por quadro | 5,50 ms (99,4% dos quadros) | **3,73 ms** (97,5%) | −32% |
| 0x40300890 por quadro | 1,03 ms | **0,30 ms** | −71% |
| CPU `rsx::thread` / `main_thread` | 95,1% / 38,3% | 93,6% / 37,8% | |

## Condições

- Build `5D2A5E2A…`, `RPCS3_RSX_DIAGNOSTICS=1` nos dois braços. Mesmo save e mesmo percurso de 90 s (início 14:55:20).
- Config clonada `config-tests/xfloat-20261024/relaxed.yml`: só `SPU XFloat Accuracy` difere da efetiva. O log confirmou `Relaxed`.
- O braço Approximate é a rodada `ab-semaddr-run1` (mesma build e diagnóstico, 14:27). Ela é coerente com as quatro rodadas OS/Approximate do dia (32,75–33,02 FPS). O ganho de +3,9 FPS é ~10× a variação entre rodadas. Mesmo assim, foi uma rodada Relaxed só.

## Visual

- Captura F12 no spawn (`spawn-R-f12-…14_53_38.png`) contra a do Approximate (`spawn-A-f12-2.png`): cenário, paredes e iluminação iguais, sem pixels/blocos de NaN (`crop-topo.png`, `crop-vitrine.png`). Diferença sutil na borda de uma sombra sobre a porta da direita, não conclusiva.
- **NPCs:** `crop-npcs.png` mostra, no Relaxed, o NPC de camisa cinza com a cabeça afundada no peito e inclinada. O jogador viu o mesmo em vários NPCs durante o percurso.

## Leitura

1. **Confirma a cadeia causal:** código SPU mais rápido → semáforos liberados antes → RSX espera menos → mais FPS. O ganho veio quase todo da espera em 0x40300870/890.
2. **A deformação é erro de precisão no código SPU de animação/skinning** (candidatos: `edgeanim`, 2,8% das amostras, e/ou `spumesh`). O Relaxed não emula "0 × Inf/NaN = 0" nem o range estendido do SPU, e esse código depende disso.
3. **Caminho promissor: precisão seletiva.** Usar Relaxed só nos módulos que seguram o RSX (os 5 estágios do pipeline e talvez o `spulight`) e manter Approximate no resto, inclusive animação. O RPCS3 não tem isso: a precisão é lida de `g_cfg.core.spu_xfloat_accuracy` em cada tradutor de instrução do `SPULLVMRecompiler.cpp`. Exige mudança no recompilador, com uma regra por conteúdo (hash do programa SPU) restrita ao jogo, e verificação visual e de NaN nos efeitos afetados.
