# Histórico do projeto inFAMOUS 2

Este histórico resume as alterações deste fork. Não substitui o histórico completo do Git. Não há release binária publicada até esta revisão.

## 24/10/2026 — Turbo v3, patch de modelos e perfil RSX

### Publicado na master

- Espera ociosa do RSX Offloader e giro curto opcional antes de dormir.
- Diagnóstico RSX com separação de esperas por endereço de semáforo.
- Override XFloat seletivo por grupo de programas SPU de BCUS98125 01.04.
- Afinidade híbrida opcional e preset Turbo v3 documentado.
- Busca direta para imagens Vulkan com uma única view.
- Patch **Disable Mesh Trimming**, de Yahfz, para o hash PPU testado; correção dos modelos confirmada pelo jogador.
- Scripts de backup, coleta e análise; leitor de ETL offline e teste de concorrência do helper de espera.
- [Relatórios de desempenho](README.md#resultados-e-evidências), incluindo experimentos sem ganho.

Código e ferramentas foram integrados pelo [PR #1](https://github.com/vhd-kilim/rpcs3-infamous2-optimized/pull/1). A integração não transforma todos os caminhos experimentais em opções recomendadas.

### Experimentos e limitações

- **Notificação de flush:** opt-in, desligada por padrão. Menor CPU, mas 34,71 FPS contra 35,77 da v3 com patch no primeiro par.
- **Busca de view única:** 36,25 contra 35,77 FPS; +1,34% observado, ainda sem repetição suficiente.
- **Relaxed global:** deformou NPCs; excluído do preset. Animação e simulação continuam em Approximate.
- **Reservas RSX menos precisas:** sem ganho mensurável; o preset mantém reservas precisas.

### Organização do repositório

- README principal em português, com escopo, resultados e instruções de acesso ao código.
- Índice técnico com estado atual, medições e registros históricos.
- Descrição do PR integrado atualizada para refletir o resultado final e a participação de ferramentas de IA.

### Ainda em investigação

- Recorte retangular compartilhado por fumaça e chama.
- Custo de NPCs/combate, física e sincronização RSX/SPU; sem patch específico entregue.
- Repetição dos benchmarks e validação em mais cenas e máquinas.

Builds Windows Release x64 e testes locais específicos foram executados. A suíte geral GoogleTest e uma campanha completa não foram validadas. Veja [perfil RSX](RSX_PROFILE_20261024.md) e [Turbo](measurements/TURBO_20261024.md).
