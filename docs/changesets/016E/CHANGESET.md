# CS016E — Governance Root-of-Trust Hardening

State: `in_progress`

Control base: `de55e0882c6400a0409b5cf881c6ee796a975cdf`

Deviation: `DEV-0005`

Normative amendment: `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md`

## Objetivo

Eliminar as brechas de autorreferência/mascaramento identificadas depois de CS016D sem tocar no runtime do produto.

## Entregas

- raiz de confiança fail-closed;
- máximos de escopo por stage;
- sealing de ACTION_SCOPE;
- paths explícitos para ações de escrita;
- diff baseado em SHA confiável, não escolhido pelo candidato;
- acceptance chain criptográfica A/B/C/D e futura E;
- verificação de provenance contra a API oficial do GitHub;
- política verificável de proteção da `main`;
- trusted verifier ratchet via `pull_request_target` após bootstrap;
- release derivado e artifact attestation obrigatória;
- SCN-REGRESSION-005 e requirements/invariants correspondentes.

## Não autorizado

- qualquer alteração de `src/`, `include/`, CMake, módulos, apps ou testes de produto;
- expansão de claims;
- release;
- reaproveitamento de CS017 R4;
- reclassificação de falhas anteriores;
- alteração retroativa de Source of Truth, Plan 1.0, Amendments 1.1–1.4 ou D-Lab Standard.

## Bootstrap boundary

Este ChangeSet introduz a própria trusted root. Portanto seu primeiro PR ainda não pode ser validado retroativamente por um `pull_request_target` que não existia na base. A aceitação é deliberadamente em duas fases:

1. integrar o hardening somente depois de a proteção bootstrap da `main` estar ativa e o PR passar os checks existentes;
2. após merge, habilitar o required check `Trusted governance root gate`, obter post-merge success e abrir a closure de CS016E com todos os SHAs/runs reais.

Até a fase 2 terminar, CS016E permanece `in_progress` e CS017 permanece bloqueado.

## Maintainer topology

O repositório operará em modo `single_maintainer`, declarado durante o bootstrap e antes de qualquer aceitação de CS016E.

Isso não autoriza autoaprovação humana. O projeto declara explicitamente que não existe reviewer humano independente e proíbe o uso de conta alternativa controlada pelo mesmo mantenedor para simular independência.

A proteção da `main` continua exigindo PR, strict required checks, app binding dos checks, enforcement administrativo, ausência de bypass rotineiro, bloqueio de force push/deletion e, no steady state, `Trusted governance root gate`.

O bootstrap merge não fecha CS016E. A closure continua exigindo um segundo PR depois que a trusted root estiver instalada na `main`.

Decision record: `docs/changesets/016E/SINGLE_MAINTAINER_DECISION.md`.
