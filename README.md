# RPCS3 — inFAMOUS 2 Optimized

Fork experimental do [RPCS3](https://github.com/RPCS3/rpcs3) dedicado a melhorar o desempenho de **inFAMOUS 2** no PC, com medições de frametimes, perfil de CPU e correções visuais verificadas em jogo.

**Estado em 24/10/2026:** Turbo v3 testada, patch de modelos validado pelo jogador e uma pequena melhoria de busca de views Vulkan em avaliação. Projeto independente da equipe RPCS3. O jogo continua sendo emulado; não há port nativo.

[Documentação técnica](docs/infamous2/README.md) · [Resultados do RSX](docs/infamous2/RSX_PROFILE_20261024.md) · [Histórico](docs/infamous2/CHANGELOG.md) · [Ferramentas](tools/infamous2/README.md)

## O que já foi feito

| Área | Alteração | Situação |
|---|---|---|
| RSX Offloader | Espera de fila ociosa e giro curto configurável antes de dormir | Testado em A/B; menor desperdício de CPU |
| SPU | Precisão XFloat seletiva por grupo de programas do jogo | Preset v3 testado; animação e simulação mantidas em Approximate |
| CPU híbrida | Afinidade específica para separar o RSX das demais threads | Testado neste i5-14400F; depende da topologia |
| Modelos | Patch **Disable Mesh Trimming**, de Yahfz | Jogador confirmou a correção de Cole e NPCs com escala elevada |
| Vulkan | Busca direta quando a imagem possui uma única view | Primeira rodada positiva; repetição pendente |
| Diagnóstico | Tempos RSX, semáforos, PresentMon e leitor offline de perfil WPR | Ferramentas e resumos publicados |

O recorte retangular de fumaça e chamas **continua em investigação**. O patch de modelos não comprova correção desse efeito. Também não há patch de IA, redução de NPCs ou otimização de física entregue.

## Resultados medidos

Máquina: **Intel Core i5-14400F, Radeon RX 9060 XT, 32 GB DDR4, Windows e Vulkan**. Jogo **BCUS98125, atualização 01.04**, escala de resolução **200%**.

| Comparação | FPS médio | 1% low | Limite da evidência |
|---|---:|---:|---|
| Offloader: referência → espera ociosa | 31,21 → 32,89 (**+5,4%**) | 22,03 → 23,16 | Quatro percursos A/B alternados |
| Referência com Offloader → Turbo v3 | 32,97 → 36,56 (**+10,9%**) | 22,78 → 24,51 | Duas rodadas v3; referência descrita no relatório |
| v3 com patch → busca de view única | 35,77 → 36,25 (**+1,34% observado**) | 23,71 → 24,04 | Uma rodada por variante, com os mesmos profilers ativos |

As linhas pertencem a testes diferentes: **os percentuais não devem ser somados**. Percursos manuais variam com câmera, NPCs, efeitos e combate; os números não garantem o mesmo ganho em outras cenas ou computadores. O 1% low usa a média do 1% de intervalos mais lentos, não o FPS do título da janela.

Consulte os [testes Turbo](docs/infamous2/measurements/TURBO_20261024.md), o [A/B do Offloader](docs/infamous2/measurements/AB_OFFLOADER_20261024.md) e o [perfil RSX](docs/infamous2/RSX_PROFILE_20261024.md) para condições, regressões e limitações.

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

O [guia técnico](docs/infamous2/README.md) lista as variáveis e a configuração Turbo v3. Elas não são todas ativadas automaticamente ao compilar. O [lançador do laboratório](tools/infamous2/Jogar-Infamous2-Otimizado.ps1) registra o preset da última build testada e faz backup verificado dos saves antes de iniciar; contém caminhos e hash específicos desta máquina e precisa ser adaptado para outra instalação.

O [patch de modelos](tools/infamous2/patches/BCUS98125_patch.yml) é restrito à versão e ao hash PPU indicados no arquivo. Veja as [instruções de aplicação](tools/infamous2/README.md#patch-de-modelos), preservando os patches que já existirem no perfil.

**Não usar Relaxed global:** houve deformação de NPCs. A espera experimental `RPCS3_INFAMOUS2_RSX_FLUSH_WAIT` permanece desligada por padrão: reduziu CPU, mas piorou FPS no primeiro teste. Diagnósticos servem para medir e não precisam ficar ligados durante o uso normal.

## Próximas investigações

- Repetir o A/B da busca de views para separar ganho pequeno de variação de cena.
- Investigar os passes compartilhados pelos efeitos de fumaça e chama recortados.
- Aprofundar o perfil das esperas RSX/SPU e dos programas associados a NPCs, sem retirar personagens, efeitos ou áudio como atalho.

O [índice técnico](docs/infamous2/README.md) separa resultados atuais dos documentos históricos de pesquisa.

## Créditos, contribuição e licença

O emulador e a maior parte deste código são trabalho dos [desenvolvedores do RPCS3](https://github.com/RPCS3/rpcs3). O patch Disable Mesh Trimming é de **Yahfz**. Este fork registra trabalho assistido por **Codex e Claude**, com percursos e verificação visual feitos pelo jogador; assistência automatizada não substitui revisão ou validação em jogo.

Para contribuir, descreva versão/hash, hardware, preset, percurso e comparação; diferencie observação de hipótese e inclua a participação de ferramentas de IA quando houver. Preserve os créditos e não envie conteúdo do jogo, saves ou rastros privados. As regras do projeto original estão no [README upstream](https://github.com/RPCS3/rpcs3#contributing).

A maioria dos arquivos usa **GNU GPL-2.0-only**; consulte [LICENSE](LICENSE) e os cabeçalhos individuais. Informações gerais do emulador: [site oficial do RPCS3](https://rpcs3.net/).
