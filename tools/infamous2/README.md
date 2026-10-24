# Ferramentas de laboratorio

Fontes dos coletores e analisadores usados neste projeto. Os scripts PowerShell sao snapshots do laboratorio Windows e possuem caminhos locais, hashes de builds e dependencias declaradas no inicio. Ajuste-os para seu ambiente antes de executar; eles nao sao um instalador universal.

## Fluxo

1. Fechar o RPCS3 e executar `Backup-Rpcs3Saves.ps1` com destino novo. Ele valida hashes e recusa links de diretorio ou substituicao de backup.
2. Confirmar exe/config/patch. `Start-AB-Run.ps1` inicia PresentMon, amostras de CPU por thread e log. A variante `turbo3` aponta a build preservada; `rsxwait` e experimental.
3. Marcar inicio/fim do percurso em `session.json/route_windows`. `Analyze-AB-Run.py` usa o inicio marcado, 90 s de duracao e descarte inicial de 3 s por padrao.
4. Para perfil por funcao, `Profile-RsxCpu-Wpr.ps1` exige elevacao, espera `wpr-start.request`, grava no maximo 90 s por padrao e encerra somente sua propria instancia WPR. O ETL pode ultrapassar 1 GB.
5. `Extract-RsxCpu.cpp` le o ETL offline, filtra TID e janela FILETIME, exige PDB compativel e exporta amostras self. Nao abre ou suspende o jogo. O TID vem de `Profile-Rpcs3Threads.py`; nao reutilizar TID de outra sessao.
6. `Finish-AB-Run.ps1` preserva o log apos fechar o jogo e executa os analisadores.

O exportador nativo usa o layout dos eventos documentado no codigo de Microsoft PerfView (`KernelTraceEventParser.cs`, classes SampledProfileTraceData/ImageLoadTraceData). Ele filtra DPC/ISR e informa perdas e enderecos nao resolvidos. Nao implementa arvore completa de stacks, codigo dinamico do JIT ou simbolos privados do driver.

Para compilar o leitor, em Developer Command Prompt x64:

```bat
cl /O2 /EHsc /std:c++20 /DUNICODE /D_UNICODE tools\infamous2\Extract-RsxCpu.cpp advapi32.lib dbghelp.lib
```

## Teste do helper de notificacao

```bat
cl /O2 /EHsc /std:c++20 tools\infamous2\tests\notification_wait.cpp
notification_wait.exe
```

O helper e o mesmo incluido pelo renderer. O teste exercita a condicao de conclusao e a corrida load-to-wait com atomicas C++20; o runtime real do RPCS3 e validado separadamente.

## Dados

PresentMon e uma dependencia externa, nao incluida. PDBs e binarios sao produzidos pela build local. PerfView nao e necessario para executar o leitor nativo.

Nao colocar ISO, EBOOT, ELF, PSARC, firmware, saves, ETL ou dumps do codigo do jogo no repositorio. Os resultados de desempenho publicados sao tabelas resumidas, nao dados de campanha ou capturas de memoria.
