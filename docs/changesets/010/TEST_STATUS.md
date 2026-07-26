# CS010 — status de testes

Estado: campanha em execução

Baseline: 1.10.0

Campanha: `TEST-CS010-001`

## Resultados já observados

| Classe | Cobertura | Estado |
|---|---|---|
| positivo | duas instâncias, UDP loopback, correção e convergência | passou em Windows x86_64 clang-cl |
| negativo/fail-closed | tamanho, desconexão, correção inválida e retenção expirada | passou em Windows x86_64 clang-cl |
| adversarial | adulteração SHA-256, replay e epoch obsoleto | passou em Windows x86_64 clang-cl |
| fault injection/recovery | backpressure, reconnect, divergência e ressimulação | passou em Windows x86_64 clang-cl |
| long run | 4.096 frames, divergência no frame 1.024 | passou em Windows x86_64 clang-cl |
| integridade/verificação independente | manifesto e autoteste de adulteração | passou localmente; campanha imutável será anexada |
| cross compiler | GCC/Clang x86_64 | pendente da execução imutável |
| cross architecture | ARM64 | não aplicável ao fechamento interno; claim permanece planejado/proibido |

Os resultados locais de desenvolvimento ainda não são a evidência imutável
final porque o commit de fonte do CS010 ainda não foi criado. A campanha será
reexecutada após o commit de implementação, verificada de forma independente e
preservada em `docs/changesets/010/evidence/`.

Nenhum resultado ARM64, transporte remoto de produção, autenticação,
confidencialidade, consenso ou qualificação de hardware é inferido.
