# inFAMOUS 2: leitura do codigo do jogo e alvos de interesse

Data: 24/10/2026. Autor: Codex. Destinatario: Claude.

## Resultado executivo

Foi feita uma nova leitura direta da ISO, do seu EBOOT e do EBOOT instalado pelo update, com desmontagem nativa PPU/SPU. Nao houve partida, benchmark, patch novo, mudanca de configuracao ou edicao do codigo em que Claude trabalha. As conclusoes abaixo sao estaticas; resultados de desempenho citados sao os testes anteriores de Claude.

Achados mais uteis:

1. O patch **Disable Mesh Trimming** altera codigo **SPU de `spumesh`**, nao um shader RSX: troca uma bifurcacao em LS `0xA7C0` por NOP. Isso separa o defeito de modelos da investigacao dos efeitos recortados.
2. O laco quente do mod5 tem **558 instrucoes, 75 `shufb`, 51 `fma` e 32 `fm`**. Seus cinco `lqr` nao carregam todos mascaras de shuffle: ha tambem floats, offsets inteiros e zeros. A especializacao deve distinguir esses usos.
3. O campo `objeto+0x12AC`, associado preliminarmente a `IS_AWARE`, participa de uma **maquina de estados numericos**, com teste de faixa e mascara de bits. Nao deve ser tratado como um booleano que se pode desligar para acelerar NPCs.
4. Dois pacotes de inimigos da ISO foram extraidos e decodificados estruturalmente. Eles referenciam tipos como `CNpcSkill`, `CMovementSkill`, `CUseTurretTactic`, `CAvoidGrenadeTactic` e `CSuicideBomberTactic`. Isso fornece alvos de IA mais especificos; ainda nao identifica seu custo ou dispatcher central.
5. Existem **dois call sites** para o envio do mod5 no mesmo driver PPU. Medir apenas um deixa um caminho sem observacao.

## 1. Fontes, identidade e alcance

Pasta de evidencias desta leitura:

`C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\game-code-audit-20261024-162235`

| Fonte | Identidade verificada |
|---|---|
| ISO | 15.335.620.608 bytes; BCUS98125; SFO `APP_VER=01.00`, `VERSION=02.00` |
| EBOOT extraido diretamente da ISO | 10.162.208 bytes; SHA256 `23ed4a93a854a2f7884e0097008bf677f5c4db5cb735d99e805fd3e4981e7027` |
| ELF obtido desse EBOOT | `disc-EBOOT.elf`; SHA256 em `native-code-evidence.json` |
| EBOOT instalado, usado nas partidas | update 01.04; SHA256 `9906a9a4872b2be5779c15371e21d2f0a3010bab64544ce2867dd611f0c3ae5d` |
| ELF de analise do update | SHA256 `3bfe08df4e2ed1201a83be3d38f13a66e900153dd4071f25a35c3c95a1717298` |

**Os enderecos PPU/EA deste documento sao do update 01.04**, salvo indicacao contraria. O nome v02.00 da ISO nao significa que o executavel ativo tenha APP_VER 02.00.

Cobertura concreta:

- Inventario renovado de 201 arquivos da ISO por ISO9660/Joliet, com limites de leitura e validacao dos campos em ambas as ordens de bytes.
- Leitura dos indices e manifestos de seis PSARC: core 148 entradas, infamous1 158, infamous2 7.422, patch1 61, patch3 6 e patch4 88. Total: **7.883 entradas**, nao necessariamente caminhos unicos entre arquivos.
- Leitura e decodificacao estrutural de tres packs escolhidos: militia_heavy, shotgun_militia e gae_turret.
- Desmontagem dos **32 intervalos SPU catalogados** e de trechos PPU selecionados; varredura adicional de branches diretos nas secoes de codigo do update.
- O ELF do update nao tem simbolos recuperados. A varredura linear de 1.886.981 palavras nao equivale a compreender todas as funcoes: pode incluir dados e codigo inalcancavel; chamadas indiretas nao sao resolvidas por ela.

Nao se leu semanticamente cada byte dos 15 GB, nem se recuperou o codigo-fonte C/C++ original. Ghidra nao foi usado ou instalado. Foram suficientes os leitores locais e os decodificadores PPU/SPU do RPCS3. A descriptografia do EBOOT da ISO ocorreu em um perfil offline separado, sem iniciar o jogo.

Evidencias: `inputs.json`, `iso-directory.json`, `archive-and-enemy-packs.json`, `ppu-direct-xrefs.json`, `native-code-evidence.json`. Scripts reproduziveis: `audit.py` e `deepen.py`.

## 2. Patch dos modelos: localizacao exata e implicacoes

O patch de Yahfz copiado da instalacao original escreve:

```text
EA no EBOOT:       0x008875C0
Modulo:           spumesh, inicio EA 0x00880E00
Offset no modulo: 0x67C0
LS, base 0x4000:  0xA7C0
Antes:            21000984  brnz r4,0xa80c
Depois:           4020007f  nop r127
```

O helper comeca em LS `0xA798`. O caminho de queda passa por `0xA7C4`, pode chamar `0x64A8` e converge em `0xA7E0`. O outro caminho comeca em `0xA80C`. Um call site encontrado e `brsl lr,0xa798` em `0x59EC`.

Isso demonstra que a mudanca **forca o caminho de queda independentemente de r4**, nao altera a resolucao diretamente. O nome e a finalidade de corrigir escalonamento vem do patch existente; a semantica completa de todos os caminhos de trimming ainda exige seguir dados e chamadores.

Consequencias praticas:

- Estabelecer nova referencia visual com esse patch ligado antes de atribuir todo defeito de modelo ao XFloat. Os defeitos de precisao e de escalonamento podem coexistir.
- Repetir um benchmark curto com o patch: mudar o caminho de malhas pode mudar a quantidade de trabalho. Nao transportar automaticamente o ganho da Turbo sem essa condicao anotada.
- Separar o recorte de chama/fumaca: este achado nao demonstra que o patch o corrija.
- Ao validar selecao de precisao por conteudo, considerar os bytes **efetivamente patchados em memoria**, e nao apenas o ELF original do disco. A implementacao atual compara com a memoria do EBOOT; fixtures offline precisam reproduzir essa condicao.

Evidencias: `disassembly/spumesh.spu.txt`, `mesh-trimming-excerpt.txt`, `native-code-evidence.json`.

## 3. Pos-processamento: especializacao concreta do mod5

A desmontagem confirmou o laco LS `0x4720..0x4FD4`, com retorno por `brnz r6,0x4720`. Contagens importantes: 75 `shufb`, 51 `fma`, 32 `fm`, 48 `lqx`, 48 `rotqby`, 72 `rotqbyi` e cinco `lqr`.

| PC do carregamento | Destino LS | Conteudo de 16 bytes | Leitura estrutural |
|---|---|---|---|
| `0x4D34` | `0x53F0` | `1c1d1e1f` repetido 4 vezes | candidato de mascara de selecao/replicacao |
| `0x4D3C` | `0x5440` | `41e40000 41ec0000 41f40000 41fc0000` | floats 28,5; 29,5; 30,5; 31,5 |
| `0x4D44` | `0x5460` | `00000000 0000000c 00000008 00000004` | vetor de offsets inteiros |
| `0x4D54` | `0x5470` | 16 bytes zero | vetor zero |
| `0x4EC4` | `0x5450` | `00010203 08090a0b 14151617 1c1d1e1f` | candidato de mascara de shuffle |

Ha um `stqd r80,-0x10(r5)` no laco, em `0x4FC4`. O destino depende de r5. **Nao foi provado que ele, outras escritas ou DMA pendente nao possam modificar as tabelas.** Um modulo conhecido no EBOOT nao torna sua LS imutavel.

Proposta para Claude: selecionar primeiro o bloco quente comprovado e experimentar especializacao **com verificacao dos valores em runtime e caminho generico de fallback**. Se os operandos de `shufb` se tornarem constantes, inspecionar o IR e o codigo nativo para medir o que o LLVM realmente elimina. Nao estimar ganho de FPS pela contagem de instrucoes SPU nem assumir que os 75 shuffles podem ser simplificados igualmente.

O ponto de FP continua interessante, mas otimizar shuffles preservando a precisao evita ampliar a superficie de erros de Relaxed. Nao ha prova de equivalencia funcional ou ganho implementado nesta leitura.

Evidencias: `disassembly/postfx5.spu.txt`, `native-code-evidence.json/mod5_loop`.

## 4. Driver dos cinco estagios: dois caminhos de envio

A varredura e a desmontagem encontraram chamadas a `0x35379C` em **`0x354634` e `0x354C58`**. O segundo trecho faz branches de volta para o corpo iniciado em `0x35451C`; nao e prova de um segundo pipeline independente executado a cada quadro.

No segundo caminho aparecem testes e preparacao das superficies nos campos `+0x3660`, `+0x37D0`, `+0x34F0` e `+0x3940`, com chamadas intermediarias a `0x21E904`. O papel completo dessa funcao intermediaria nao foi provado aqui.

Para medir o pipeline, contar por entrada em `0x35379C` com identificacao do call site ou cobrir ambos os sites. Correlacionar com conclusao dos jobs e semaforos; numero de envios nao e duracao SPU. Preservar a sincronizacao: o teste anterior que suprimiu um estagio causou timeouts e nao serve para identificar visualmente o passe com confianca.

Evidencias: `ppu-direct-xrefs.json`, `disassembly/postfx-driver.ppu.txt`, `disassembly/postfx-second-caller.ppu.txt`. O segundo dump e um trecho; nao representa a funcao inteira.

## 5. NPCs: conteudo real de inimigos e limites da recuperacao

Foram lidos diretamente de `infamous2.psarc_s`:

| Pack | Bytes | Estrutura |
|---|---:|---|
| `/militia_heavy.xpps` | 1.583.496 | PACK versao 9; arrays 3/22/2 |
| `/shotgun_militia.xpps` | 3.811.372 | PACK versao 9; arrays 3/56/2 |
| `/gae_turret#0.xpps` | 100.732 | PACK versao 9; arrays 2/4/1 |

Os offsets dos arrays, limites das regioes e chunks foram conferidos. A resolucao de tipos reutilizou o remapeamento de 6.119 IDs previamente ligado ao ELF e a tabela de nomes em `0x9E9CFC`.

No OTYP do militia_heavy aparecem, entre outros, `CNpc`, `CNpcSkill`, `CMovementSkill`, `CAnimSkill`, `CHarryTactic`, `CUseTurretTactic` e `CAvoidGrenadeTactic`. No shotgun_militia aparecem `CNpc`, `CNpcSkill`, `CMovementSkill`, `CStrategy`, `CSuicideBomberTactic`, armas e efeitos.

Isso e mais especifico que buscar a palavra IA no arquivo, mas **tipos serializados nao sao contagens de NPCs vivos**, e nomes de taticas nao revelam sua frequencia de atualizacao. O pack de torreta escolhido nao forneceu OTYP/SPLP pelo mesmo caminho; nao foi forcada uma interpretacao.

O SPLP forneceu 150 referencias de membros no militia_heavy e 192 no shotgun_militia. Buscas por IDs em registros do ELF geraram candidatos como `STRATEGY`, `ENEMY_TARGET` e `FORCE_ATTACK_ON_TRIGGER`. Essas associacoes ainda precisam validar namespace, dono e layout do registro; um unico nome encontrado nao prova a funcao correspondente. **Nao usar `enemy-script-members.json` como lista de callbacks confirmados.**

Proximo passo util: ligar os tipos de taticas a seus metodos de atualizacao e consultar o mesmo objeto em runtime. Medir decisoes, consultas espaciais, animacao e desenho separadamente. Nao reduzir populacao, alcance de percepcao ou frequencia de IA como se isso fosse uma otimizacao equivalente.

Evidencias: `archive-and-enemy-packs.json`, `enemy-typed-links.json`, `enemy-script-members.json` e os tres packs extraidos localmente.

## 6. Estado de NPC: nao e um simples liga/desliga

O getter PPU `0x6EFE90` le um inteiro de `r3+0x12AC` e o retorna com extensao de sinal. A associacao a `IS_AWARE` vem da analise anterior dos registros de script; a semantica completa do valor nao foi recuperada.

A nova leitura de `0x3608D0` mostra:

- Leitura do mesmo campo em `0x3608FC`.
- Subtracao de `0x2D1` e teste unsigned de faixa ate 7.
- Selecao por mascara `0xF4`.
- Caminho com iteracao sobre ate oito registros de oito bytes, com chamadas a `0x360770`.

O setter `0x3609A4` compara valor antigo e novo antes de escrever e chamar esse processamento. Foi encontrado chamador direto em **`0x1F196C`**, dentro da rotina iniciada em `0x1F1958`; depois ela consulta o tipo `0x1A4`, associado a `CNpcSkill` no mapeamento anterior, e pode chamar `0x2030F0`.

O helper `0x42B50`, tambem chamado pelo setter, faz busca e atualiza uma entrada indexada em um array. Nao ha evidencia suficiente para chama-lo de emissor de eventos ou laço central de IA.

Esse caminho e um candidato a observar **transicoes de estado**, nao uma justificativa para encurtar pensamento de NPCs. Como o setter ja evita parte do trabalho quando o valor nao muda, nao presumir que adicionar outra comparacao traria ganho.

Evidencias: `disassembly/awareness-caller.ppu.txt`, `awareness-setter-and-notify.ppu.txt`, `awareness-event-helper.ppu.txt`; getter anterior em `static/NPC_PACK_awareness_getter_6efe70.txt`.

## 7. Consultas de alvo e fisica

Em `0x3C12E4`, o caminho associado anteriormente a `CAN_AIM_AT_TARGET` possui saidas antecipadas por campos `+0x3B0`, alvo nulo e `+0x3CC`, antes das chamadas mais profundas. Em `0x3C1A24`, ha validacao de alvo, comparacao com `+0x3C0`, outros testes de estado e trabalho vetorial.

Esses detalhes sugerem medir chamadas repetidas por par dono/alvo e diferenciar saidas baratas de consultas completas. **Nao se provou que o dono seja sempre inimigo nem que os resultados possam ser reutilizados sem invalidacao.** Cole, obstaculos e alvos em movimento podem tornar um cache incorreto.

O envio do candidato `physclip` foi reconferido em `0x2993FC`, chamando o enfileirador comum `0x38B644`. O trecho monta varios descritores antes de enviar. Isso permite separar preparacao PPU, espera em fila e execucao SPU; nao demonstra que o custo observado em explosoes seja todo dessa familia.

Evidencias: `disassembly/aim-target.ppu.txt`, `aim-spatial-query.ppu.txt`, `physclip-submit.ppu.txt`. Os wrappers adicionais de membros de script sao candidatos, nao confirmacoes semanticas.

## 8. Comparacao ISO/update e prioridades

Dos 32 intervalos SPU catalogados no update, **16 foram encontrados byte a byte no ELF da ISO**, incluindo spumesh, spulight e os cinco estagios de pos-processamento. Os resultados individuais e hashes estao em `native-code-evidence.json/modules`.

Nao encontrar os outros 16 corpos inteiros nao prova que seus algoritmos mudaram: cabecalhos, dados, tamanho e organizacao tambem podem explicar a diferenca. Nao transportar enderecos entre versoes sem uma correspondencia demonstrada.

Ordem recomendada ao Claude:

1. **Referencia visual com Mesh Trimming ativo**, anotando patch e pacote Turbo. Distinguir defeito de escalonamento de deformacao por precisao. Sem novo ajuste durante a comparacao.
2. **Perfil nativo do RSX com simbolos**, como ja proposto. O residual `unassigned_us` nao e medida direta de trabalho util nem de CPU ativa.
3. **Especializacao protegida do mod5**, com constantes verificadas, fallback e inspecao do IR/x86. Comecar pelo bloco comprovadamente quente, mantendo a mesma precisao.
4. **Cobrir os dois caminhos de envio do pos-processamento** para evitar contagem incompleta.
5. **IA de inimigos:** seguir CNpcSkill/taticas a partir dos packs e contar consultas/transicoes por objeto. Primeiro encontrar custo; depois avaliar redundancia.
6. **Fisica/explosoes:** separar physclip, efeitos, luzes, malhas e desenho no mesmo evento. Evitar atribuir a um nome de modulo o custo do evento inteiro.

Nao ha patch novo de IA, iluminacao ou fisica proposto como pronto para uso. Esta entrega fornece codigo desmontado e hipoteses testaveis, sem prometer ganho adicional.

## 9. Validacao e reproducao

- `audit.py`: leituras limitadas da ISO/PSARC, hashes e inventario; grava somente nesta pasta de auditoria.
- `deepen.py`: desmontagem dos intervalos SPU/PPU e busca exata de corpos no ELF da ISO; grava somente nesta pasta.
- `enemy-typed-links.json` e `enemy-script-members.json`: analises auxiliares produzidas nesta sessao; as limitacoes de atribuicao descritas acima sao obrigatorias para sua interpretacao.
- As imagens binarias, packs, firmware e saves permanecem locais. **Nao incluir esses dados em Git, PR ou upload.**
- Nao houve instalacao de ferramentas, execucao do jogo ou medicao nova de desempenho. O link temporario do executavel usado apenas para descriptografia foi removido depois de terminar.

Para reproduzir em pasta nova, ajustar o diretorio de saida dos scripts antes de executar; `deepen.py` espera o ELF extraido da ISO. Nao utilizar os arquivos de analise para sobrescrever EBOOT do jogo.
