# ChangeSet 016B — status de validação

State: accepted

## Decisão

A árvore candidata registra a aceitação de CS016B com base no source corrigido
`b11f91fc0db5610c7e195ff1a282e04aee80e987`, validado no GitHub Actions
workflow `31596214152`, evento `push`, conclusão `success`.

Essa decisão cobre exclusivamente a correção de governança do self-test de
lifecycle. A incorporação oficial continua condicionada aos gates de PR e ao
gate pós-merge da `main`; falha nesses gates impede considerar CS016B ativo na
branch principal.

CS017 permanece interrompido durante esta integração. EV-00 na `main` oficial
permanece `not_started`.

## Gates da decisão de source

| Gate | Estado | Evidência |
|---|---|---|
| D-Lab action authorization self-test | PASS | run `31596214152` |
| SCN-REGRESSION-001 preservado | PASS | run `31596214152` |
| SCN-REGRESSION-002 lifecycle fixtures | PASS | run `31596214152` |
| D-Lab governance self-test | PASS | run `31596214152` |
| D-Lab governance verifier | PASS | run `31596214152` |
| Required evolution amendments gate | PASS | run `31596214152` |
| Evolution verifier self-test | PASS | run `31596214152` |
| Evolution verifier | PASS | run `31596214152` |
| Product contract verifier | PASS | run `31596214152` |
| Product assurance verifier | PASS | run `31596214152` |
| Manifest | PASS | run `31596214152` |
| Evidence upload | PASS | run `31596214152` |
| Evidence manifest | PASS candidate | `evidence/EVIDENCE_MANIFEST_ACCEPTED.json` |
| PR gate | NOT_TESTED | integração ainda não executada |
| Post-merge main gate | NOT_TESTED | integração ainda não executada |

`NOT_TESTED` nunca equivale a aprovação. Os dois últimos gates são gates de
incorporação, não evidência retroativa do source `b11f91fc...`.

## Evidência vinculada

- accepted source candidate: `b11f91fc0db5610c7e195ff1a282e04aee80e987`;
- source tree: `33a8c0193b2de00a9d52ea8bdf1ed09971c0714e`;
- run: `31596214152`;
- job: `94112263299`;
- artifact: `9141135665`;
- artifact digest: `sha256:1be80d276c7915196d244c65a4aae828492f1c4702481e4e7b4c09d76bff77af`;
- evidence manifest: `docs/changesets/016B/evidence/EVIDENCE_MANIFEST_ACCEPTED.json`.

## Falha que permanece preservada

A tentativa inicial de CS017 no source
`bfafa432ad4dc7c402753293da080fc6d920c8ce`, run `31594048822`, permanece
registrada como `failure` em `evidence/triggering-failure.json`.

Ela não é reclassificada após esta correção e não constitui campanha
qualificante de EV-00.

## EVREQ-072 / INV-EV-028

EVREQ-072 está `verified` porque o próprio self-test executado em CI construiu
e verificou fixtures explícitos para os estados `not_started` e `in_progress`,
inclusive com todos os amendments simulados como aceitos.

INV-EV-028 permanece `active`.

## Limites

CS016B não altera nem qualifica:

- `src/` ou `include/`;
- ABI/Host SDK;
- replay/rollback/snapshots;
- serialização ou semântica canônica;
- claims públicas;
- release;
- a campanha de baseline EV-00;
- ambientes não executados.

Nenhum plano anterior ou D-Lab Standard foi alterado para obtenção desta
decisão.
