# A/B de agendamento de threads — 24/10/2026

**Resultado:** o modo atual, **Operating System sem afinidade, é o melhor**. Os dois braços alternativos pioraram o FPS e os engasgos, e nenhum reduziu a espera do RSX pelos semáforos das SPUs. **Não mudar a config.**

| Braço | FPS médio | 1% low | P99 | > 50 ms | Espera de semáforo (% RSX) | 0x40300870 por quadro | 0x40300890 por quadro |
|---|---:|---:|---:|---:|---:|---:|---:|
| **Operating System** (atual) | **32,97** | **22,78** | **39,9 ms** | 3 | 21,5% | 5,50 ms (99,4% dos quadros) | 1,03 ms |
| RPCS3 Alternative Scheduler | 32,15 (−2,5%) | **16,60 (−27%)** | 46,8 ms | 21 | 19,3% | 5,24 ms (96,6%) | 0,77 ms |
| Só núcleos P (afinidade 0xFFF, modo OS) | **30,02 (−8,9%)** | 20,31 (−11%) | 44,8 ms | 8 | 20,8% | 5,92 ms (99,3%) | 1,03 ms |

Referência de variação entre rodadas do braço OS na mesma build/percurso: FPS 32,75–33,02 em quatro rodadas ao longo do dia (A/B do Offloader e rodada de semáforos). As diferenças acima são bem maiores que isso. A rodada de controle OS extra foi dispensada, porque a conclusão é manter o padrão.

## Condições

- Build `rpcs3.exe` `5D2A5E2A…` (Offloader + diagnóstico por endereço), `RPCS3_RSX_DIAGNOSTICS=1` em todos os braços (overhead desprezível).
- Mesmo save e mesmo percurso de 90 s; análise de [início + 3 s, início + 90 s].
- **Alternativo:** config clonada `config-tests/scheduler-20261024/alt.yml`, diferente da efetiva só em `Thread Scheduler Mode`. No i5-14400F (`intel_ht`), o RPCS3 prende PPU/SPU/RSX nos lógicos ímpares 1, 3, 5, 7, 9 e 11 (uma thread por núcleo P) mais 13 e 15 (dois núcleos E): 8 lógicos para ~6,6 núcleos de carga.
- **Só P:** `efetiva.yml` + `ProcessorAffinity = 0xFFF` (lógicos 0–11 = 6 núcleos P com HT) aplicada logo após o início do processo.
- Topologia lida via `GetLogicalProcessorInformationEx`: núcleos 0–5 = P com SMT (máscaras 0x3…0xC00), núcleos 6–9 = E (0x1000…0x8000).

## Leitura

- **Os núcleos E ajudam.** Tirá-los (só P) custou ~9% de FPS: as 6 SPUs + RSX + PPU + Offloader + compiladores precisam de mais que 12 lógicos de núcleo P.
- **Evitar o Hyper-Threading também não compensa:** o Alternativo deixa 8 lógicos para a mesma carga e gera muitos engasgos.
- **A espera nos semáforos (≈ 5–6 ms por quadro) quase não muda com o agendamento.** O atraso das SPUs não vem de posicionamento em núcleo; vem da quantidade de trabalho SPU antes da liberação. O caminho para reduzir a espera é acelerar o código SPU (pipeline mod1–mod5, em especial o mod5) ou o que mais precede a liberação desses semáforos.
