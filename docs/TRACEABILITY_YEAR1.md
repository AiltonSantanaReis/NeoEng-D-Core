# NeoEng D-Core — Matriz de rastreabilidade do projeto standalone

| Requisito do plano | Implementação principal | Evidência executável |
|---|---|---|
| Y1-O1 Runtime S[t+1] = f(S[t], I[t]) | `simulation.*`, `types.hpp`, `year1_contract.hpp` | `neoeng_tests`, `neoeng_dcore_preclosure replay` |
| Y1-O2 ECS/alocação | worlds, arenas, allocators e históricos em `include/neoeng/core` | benchmarks históricos; `neoeng_v28_raa_allocation_probe` |
| Y1-O3 rollback 8 frames | `rollback.*`, `interactive_rollback.*`, históricos paginados | `neoeng_v28_bare_metal_rollback`, `neoeng_dcore_preclosure network` |
| Y1-O4 laboratório numérico | `uncertainty_lab.*`, `fixed_raa_*`, oracles | ferramentas `neoeng_v28_raa_*`, `neoeng_v28_interval_arithmetic_audit` |
| Y1-O5 300 frames | `snapshot_store.*`, `paged_*history*`, rollback | `neoeng_dcore_preclosure history` |
| Y1-G1 Replay | schema e hash canônicos | teste `y1_replay_smoke`; campanha integral manual |
| Y1-G2 Cross-arch | serialização little-endian e hashes | scripts ARM64 históricos; gate permanece parcial |
| Y1-G3 Rollback | runner com p50/p95/p99/máximo | `neoeng_v28_bare_metal_rollback`; campanha física necessária |
| Y1-G4 Memória | históricos limitados e probes | `y1_history_smoke`; soak físico continua ausente |
| Y1-G5 Rede | reconciliação determinística simulada | `y1_network_smoke` |
| Y1-G6 Congelamento | `year1_contract.hpp`, schema v1 e migração | `neoeng_dcore_preclosure replay`; registros em `docs/records` |

## ChangeSet 001 — requisitos operacionais transversais

| Requisito do plano | Implementação | Evidência |
|---|---|---|
| 6.7 Segurança do netcode | `network_security.*`, `operational_runtime.*` | `neoeng_operational_hardening_tests`, `neoeng_network_packet_fuzz_smoke` |
| 10.4 Time-travel debugger | `observability.*`, `neoeng_time_travel_cli` | testes de navegação, diff, correlação e export JSON |
| 11.2 Autenticação e anti-replay | HMAC-SHA256, sessão, janela de 64 sequências | vetor RFC 4231, replay duplicate/too-old |
| 11.2 Limites, timeout e rate limiting | `NetworkSecurityLimits`, token bucket e expiração | testes de tamanho, capacidade, timestamp e burst |
| 11.2 Fuzzing do parser | parser total e fuzz determinístico | 100.000 casos por compilador na validação do changeset |
| 13.2 Observabilidade | `TraceBuffer`, correlation ID, códigos estáveis | testes de retenção, overwrite e consulta |
| 13.2 Recuperação | `RecoveryController`, `OperationalRuntime` | fault injection para device lost, I/O, rede, malformed e OOM |
| 5.4 Perfis P0-P3 | `hardware_profile.*`, probe e script Windows | gate unqualified/passed/failed; P1 2,0 ms e 0,1 ms |


## ChangeSet 002 — lifecycle de sessão e recovery host contract

| Requisito do plano | Implementação | Evidência |
|---|---|---|
| 11.2 Handshake/autenticação | `session_security.*`, hello v1 autenticado | teste de handshake, probe e fuzz smoke |
| 11.2 Anti-downgrade/replay | intervalo/cipher autenticados e replay cache por nonce | mutação de protocolo e replay do Client Hello |
| 11.2 Rotação/revogação | `SessionKeyRing`, epochs e `revoke_key_and_sessions` | rotação 1→2, sessão antiga e revogação conjunta |
| 11.2 Autorização | `SessionRole` e máscara por root key | teste Player/Spectator e role reject |
| 13.2 Trace de sessão/recovery | novos `TraceCode`s e correlation ID | testes do `OperationalRuntime` |
| Critério não funcional de recuperação | `recovery_contract.*`, geração e acknowledgements | stale ack, tipo inválido, halt e JSON v1 |
| Recuperação por checkpoint | `RollbackEngine::restore_checkpoint` e `truncate_after` | frame 3 restaurado para frame 2 e nova ramificação |

## ChangeSet 003 boundary note

The Year-1 corpus remains the canonical source baseline for the D-Core library. The optional View Lab does not reclassify Year-2 renderer code as Year-1 core code. Its two imported files are isolated under `modules/view_lab/vendor/year2`, recorded in `audit/YEAR2_EXTRACTION_LEDGER.json`, and used only by the read-only companion target.

## ChangeSet 004 — integridade e cadeia de evidências

| Requisito do plano | Implementação | Evidência |
|---|---|---|
| Evidência criptográfica de estado | `crypto_hash.*`, `canonical_state_sha256` | vetores SHA-256, testes GCC/Clang |
| Merkle Root e localização de divergência | `state_merkle_sha256`, provas por chunk | prova válida, conteúdo/sibling/orientação adulterados |
| State Signature desacoplada | `EvidenceSigner`, `EvidenceSignatureVerifier` | provider HMAC test-only e rejeição de assinatura alterada |
| Cadeia auditável | `EvidenceChain`, hash do envelope anterior e `EvidenceChainAnchor` | remoção interna, reordenação e alteração; truncamento/anexação detectados contra âncora confiável |
| Rollback com ramificação explícita | `EvidenceChain::fork_from` | branch ID, hash e frame do pai validados |
| Trace correlacionável | novos `TraceCategory::Evidence` e `TraceCode`s | criação, falha, cadeia quebrada e prova rejeitada |

## ChangeSet 005 — rastreabilidade adicional

- Requisito de trace correlacionável: `include/neoeng/core/observability.hpp`, `src/observability.cpp`.
- Orçamentos e divergências: `include/neoeng/core/diagnostics.hpp`, `src/diagnostics.cpp`.
- Pacote de suporte reproduzível: `include/neoeng/core/support_bundle.hpp`, `src/support_bundle.cpp`.
- Verificação independente: `scripts/verify_support_bundle.py`.
- Validações diferidas: `audit/DEFERRED_VALIDATION_GATES.json`.
- Contratos: `docs/contracts/OBSERVABILITY_V2.md` e `docs/contracts/SUPPORT_BUNDLE_V1.md`.
