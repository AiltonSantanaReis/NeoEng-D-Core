# ChangeSet 012 — fechamento temporal e efeitos externos

Baseline: 1.12.0
Campanha: `TEST-CS012-001`

## Requisitos no escopo

- `DCORE-TIME-002`
- `DCORE-TIME-003`
- `DCORE-DIV-002`
- `DCORE-OBS-001`

## Entrega

- recorder temporal durável, append-only e encadeado por SHA-256;
- recuperação e verificação independente com rejeição de adulteração;
- branch explícita e retenção autorizada dos exports temporal/evidência;
- protocolo prepare/confirm/commit/compensate para efeitos externos;
- rollback que descarta intents preparados e denuncia efeitos já confirmados;
- cobertura completa do único schema canônico prometido, `WorldState v1`;
- orçamento automático nos nove caminhos obrigatórios declarados;
- ABI C preservada por extensão aditiva dos códigos de trace.

## Não objetivos

Não são promovidos: histórico ilimitado em memória, exatamente-uma-vez sem
host conforme, reversão de efeito irreversível, anchor externo embutido, ARM64
ou desempenho universal.

## Critério de saída

Testes positivos, negativos, adulteração, recuperação e long run devem passar
em Windows Clang e Linux x86_64 GCC/Clang sobre a mesma fonte. O verificador
independente deve recalcular o manifesto e aceitar os resultados sem inferir
outro hardware ou arquitetura.
