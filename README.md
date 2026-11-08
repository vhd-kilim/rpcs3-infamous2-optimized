# RPCS3 — inFAMOUS 2 Optimized

Fork experimental do [RPCS3](https://github.com/RPCS3/rpcs3) dedicado a melhorar o desempenho de **inFAMOUS 2** no PC, com medições de frametimes, perfil de CPU e correções visuais verificadas em jogo.

**Estado em 24/10/2026:** Turbo v3, patch de modelos, envio antecipado de comandos Vulkan v2 e especialização de tabelas SPU publicados. O envio antecipado registrou **+5,3%** no A/B; a especialização restrita de tabelas registrou **+3,4%** em outra comparação. Os experimentos são opcionais, com limites de validação descritos abaixo. Projeto independente da equipe RPCS3; o jogo continua sendo emulado.

[Documentação técnica](docs/infamous2/README.md) · [Testes mais recentes](docs/infamous2/measurements/AB_20261024.json) · [Perfil RSX anterior](docs/infamous2/measurements/RSX_AB_20261024.json) · [Ferramentas](tools/infamous2/README.md)

## O que já foi feito

| Área | Alteração | Situação |
|---|---|---|
| RSX Offloader | Espera de fila ociosa e giro curto configurável antes de dormir | Testado em A/B; menor desperdício de CPU |
| SPU | Precisão XFloat seletiva por grupo de programas do jogo | Preset v3 testado; animação e simulação mantidas em Approximate |
| CPU híbrida | Afinidade específica para separar o RSX das demais threads | Testado neste i5-14400F; depende da topologia |
| Modelos | Patch **Disable Mesh Trimming**, de Yahfz | Jogador confirmou a correção de Cole e NPCs com escala elevada |
| Vulkan | Busca direta quando a imagem possui uma única view | Primeira rodada positiva; repetição pendente |
| Vulkan | Envio antecipado v2, antes de esperas reais do jogo e em trocas de render target | +5,3% no A/B alternado; opção `EARLY_SUBMIT=256` |
| SPU LLVM | Substituição de leituras LQR de tabelas por constantes verificadas na entrada | +3,4% no teste restrito; experimental, sem fallback automático |
| Diagnóstico | Tempos RSX, origem dos readbacks, escritores de semáforos e pilhas WPR | Identificados `spulight` e `edgepost` nos pontos de sincronização medidos |

O recorte retangular de fumaça e chamas **continua em investigação**. O patch de modelos não comprova correção desse efeito. Também não há patch de IA, redução de NPCs ou otimização de física entregue.

## Resultados medidos

Máquina: **Intel Core i5-14400F, Radeon RX 9060 XT, 32 GB DDR4, Windows e Vulkan**. Jogo **BCUS98125, atualização 01.04**, escala de resolução **200%**.

| Comparação | FPS médio | 1% low | Limite da evidência |
|---|---:|---:|---|
| Offloader: referência → espera ociosa | 31,21 → 32,89 (**+5,4%**) | 22,03 → 23,16 | Quatro percursos A/B alternados |
| Referência com Offloader → Turbo v3 | 32,97 → 36,56 (**+10,9%**) | 22,78 → 24,51 | Duas rodadas v3; série anterior ao patch de modelos |
| v3 com patch → busca de view única | 35,77 → 36,25 (**+1,34% observado**) | 23,71 → 24,04 | Uma rodada por variante, com os mesmos profilers ativos |
| Turbo v3 + patch + views → envio antecipado v2 | 37,10 → 39,07 (**+5,3%**) | 25,96 → 26,06 | Mesmo executável, duas rodadas por braço; diagnóstico RSX desligado |
| Envio antecipado v2 → LSCONST restrita, v1 | 39,35 → 40,68 (**+3,4%**) | 26,83 → 27,11 | Mesmo executável, duas rodadas por braço; resultado da v1 |

As linhas pertencem a testes diferentes: **os percentuais não devem ser somados**. Percursos manuais variam com câmera, NPCs, efeitos e combate; os números não garantem o mesmo ganho em outras cenas ou computadores. O 1% low usa a média do 1% de intervalos mais lentos, não o FPS do título da janela.

Na v2 da LSCONST, o grupo restrito repetiu **40,68 FPS** em média; adicionar `mesh,vis` não trouxe ganho. Adicionar `fx,spudraw` causou divergências, com crash em uma tentativa anterior: esses grupos **não fazem parte da configuração de LSCONST indicada**. Isso é diferente da seleção de precisão XFloat, que tem sua própria variável e grupos.

Consulte o [guia técnico](docs/infamous2/README.md) e os [dados das rodadas recentes](docs/infamous2/measurements/AB_20261024.json) para condições e limites da comparação. Ainda não há validação de 45 FPS estáveis ou de toda a campanha.

## Como usar este repositório

A branch **`master`** reúne o trabalho publicado. `rpcs3/` e `Utilities/` contêm código do emulador; `docs/infamous2/` reúne os resultados; `tools/infamous2/` contém os instrumentos do laboratório.

**Ainda não há pacote de instalação publicado em Releases.** O botão **Code → Download ZIP** baixa o código-fonte, não um RPCS3 pronto para jogar.

Para compilar este fork, clone também os submódulos e siga [BUILDING.md](BUILDING.md):

```sh
git clone --recurse-submodules https://github.com/vhd-kilim/rpcs3-infamous2-optimized.git
cd rpcs3-infamous2-optimized
```

A validação deste projeto foi feita em Windows x64. Os ajustes específicos deste fork ainda não foram validados em outros sistemas. Você precisa fornecer sua própria cópia do jogo e o firmware necessário; esse conteúdo não é distribuído aqui.

### Preset e patch

O [guia técnico](docs/infamous2/README.md) lista o preset Turbo v3 e as opções novas: envio antecipado `RPCS3_INFAMOUS2_EARLY_SUBMIT=256` e LSCONST restrita `RPCS3_INFAMOUS2_LSCONST=light,edgepost,postfx`. Essas opções **não são ativadas por padrão** ao compilar. A LSCONST continua experimental: uma divergência gera aviso, mas ainda não aciona um caminho genérico seguro.

O [lançador versionado](tools/infamous2/Jogar-Infamous2-Otimizado.ps1) é um snapshot anterior, fixado na v3 + busca de views; não ativa as duas novidades. Contém caminhos e hash locais e precisa ser adaptado para outra instalação. Não há instalador universal ou atualização automática de executáveis neste repositório.

O [patch de modelos](tools/infamous2/patches/BCUS98125_patch.yml) é restrito à versão e ao hash PPU indicados no arquivo. Veja as [instruções de aplicação](tools/infamous2/README.md#patch-de-modelos), preservando os patches que já existirem no perfil.

**Não usar Relaxed global:** houve deformação de NPCs. A espera experimental `RPCS3_INFAMOUS2_RSX_FLUSH_WAIT` permanece desligada por padrão: reduziu CPU, mas piorou FPS no primeiro teste. Diagnósticos servem para medir e não precisam ficar ligados durante o uso normal.

## Próximas investigações

- Implementar recuperação segura da LSCONST quando as tabelas divergirem e verificar alterações dentro das funções.
- Melhorar a identificação de recargas de jobs SPU antes de usar suas durações como medidas exatas.
- Repetir os testes em mais cenas e avaliar o 1% low, que melhorou menos que o FPS médio nas rodadas recentes.
- Investigar os passes compartilhados pelos efeitos de fumaça e chama recortados.
- Aprofundar o perfil das esperas RSX/SPU e dos programas associados a NPCs, sem retirar personagens, efeitos ou áudio como atalho.

O [índice técnico](docs/infamous2/README.md) reúne o preset e as condições dos testes publicados.

## Créditos, contribuição e licença

O emulador e a maior parte deste código são trabalho dos [desenvolvedores do RPCS3](https://github.com/RPCS3/rpcs3). O patch Disable Mesh Trimming é de **Yahfz**. Este fork registra trabalho assistido por **Codex e Claude**, com percursos e verificação visual feitos pelo jogador; assistência automatizada não substitui revisão ou validação em jogo.

Para contribuir, descreva versão/hash, hardware, preset, percurso e comparação; diferencie observação de hipótese e inclua a participação de ferramentas de IA quando houver. Preserve os créditos e não envie conteúdo do jogo, saves ou rastros privados. As regras do projeto original estão no [README upstream](https://github.com/RPCS3/rpcs3#contributing).

A maioria dos arquivos usa **GNU GPL-2.0-only**; consulte [LICENSE](LICENSE) e os cabeçalhos individuais. Informações gerais do emulador: [site oficial do RPCS3](https://rpcs3.net/).
