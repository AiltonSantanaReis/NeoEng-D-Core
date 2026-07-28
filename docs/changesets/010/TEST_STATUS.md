# CS010 — status de testes

Estado: aprovada no escopo declarado

Baseline: 1.10.0

Campanha: `TEST-CS010-001`

Fonte executada: `c3efbcc54e8317da82e645f7b223d49e25b3d579`

GitHub Actions aprovado:
`https://github.com/AiltonSantanaReis/NeoEng-D-Core/actions/runs/30187433814`

## Resultados

| Classe | Cobertura | Estado |
|---|---|---|
| positivo | duas instâncias, UDP loopback, correção e convergência | passou em Windows clang-cl, Linux GCC e Linux Clang |
| negativo/fail-closed | tamanho, desconexão, correção inválida e retenção expirada | passou |
| adversarial | adulteração SHA-256, replay e epoch obsoleto | passou |
| fault injection/recovery | backpressure, reconnect, divergência e ressimulação | passou |
| long run | 4.096 frames, divergência no frame 1.024 | passou nos três builds |
| integridade/verificação independente | manifesto, recalculo e autoteste de adulteração | passou nos três builds |
| instalação | consumidor CMake externo do target exportado | passou em Windows clang-cl |
| cross compiler | GCC/Clang x86_64 | passou; `raw-probe.json` byte a byte idêntico |
| cross architecture | ARM64 | não aplicável ao fechamento interno; claim permanece planejado/proibido |

O build Windows mínimo configurado executou 19/19 testes. O probe em cada
compilador registrou duas instâncias, 4.096 frames, divergência deliberada no
frame 1.024, localização em `position.x`, reconciliação e igualdade canônica
final.

## Evidência

- `evidence/windows-x86_64-clang-20260726/`
- `evidence/linux-x86_64-gcc-20260726/`
- `evidence/linux-x86_64-clang-20260726/`
- `evidence/cross-compiler-20260726/cross-compiler-summary.json`
- `evidence/github-actions-run-30187433814.json`

Cada campanha contém identidade de fonte/build, configuração, resultados
brutos, resumo, limitações, manifesto SHA-256 e relatório do verificador
independente.

## Ocorrência de desenvolvimento

O run inicial `30187291723`, sobre o commit `3bfab34`, foi reprovado pelo GCC
porque `-Wconversion -Werror` detectou promoção inteira no decoder little-endian.
O Clang daquele run passou, mas a campanha inteira foi rejeitada. O decoder foi
corrigido sem relaxar warnings no commit `c3efbcc`; o run substituto
`30187433814` aprovou GCC, Clang, verificadores e comparação byte a byte.

Nenhum resultado ARM64, transporte remoto de produção, autenticação,
confidencialidade, consenso ou qualificação de hardware é inferido.
