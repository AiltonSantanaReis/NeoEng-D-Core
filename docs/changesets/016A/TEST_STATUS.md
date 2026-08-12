# ChangeSet 016A — status de validação

State: accepted

## Decisão

O amendment normativo D-Lab v2 foi aceito com base no source commit
`eaa37e84d6e5a32830b01177ee0531b260ea97b5` e no workflow GitHub Actions
`31591410348`, evento `push`, conclusão `success`.

A decisão aceita somente governança. EV-00 permanece `not_started`; CS017 não
foi iniciado; nenhuma alteração de runtime, ABI, Host SDK, replay, rollback,
snapshot, serialização, semântica canônica, claim pública ou release é inferida.

## Gates da decisão de aceitação

| Gate | Estado | Evidência |
|---|---|---|
| D-Lab action authorization self-test | PASS | run `31591410348` |
| D-Lab governance self-test | PASS | run `31591410348` |
| D-Lab governance verifier | PASS | run `31591410348` |
| Anti-skip PRE-CS017 enquanto CS016A `in_progress` | PASS | run `31591410348` |
| Evolution verifier self-test | PASS | run `31591410348` |
| Evolution verifier | PASS | run `31591410348` |
| Product contract verifier | PASS | run `31591410348` |
| Product assurance verifier | PASS | run `31591410348` |
| Manifest | PASS | run `31591410348` |
| GitHub Actions candidato | PASS | run `31591410348` |
| Evidence manifest | PASS | `docs/changesets/016A/evidence/EVIDENCE_MANIFEST.json` |

## Evidência vinculada

- source commit aceito: `eaa37e84d6e5a32830b01177ee0531b260ea97b5`;
- run: `31591410348`;
- job: `94097010987`;
- artifact: `9139254445`;
- artifact digest: `sha256:a97f54c890b98fdeeebb60a49c0063203988c2095be4e38b4c4f36317b77ee8f`;
- evidence manifest: `docs/changesets/016A/evidence/EVIDENCE_MANIFEST.json`.

## Falha preservada durante reconciliação

O primeiro candidato `b18215c61e5e786dfaf4b4e2868779ea02bfa1ae`, run
`31591359623`, passou os novos gates, evolution verifier, product contract e
product assurance, mas falhou no `MANIFEST.sha256` deliberadamente ainda não
reconciliado. O job controlado atualizou somente o manifesto no commit
`0541efcc1ad490ee2517e0732ca99cd11b0f60c8`. O estado reconciliado foi então
validado independentemente pelo source commit aceito acima.

A falha inicial permanece registrada em
`evidence/github-actions-run-31591410348/reconciliation-history.json` e não foi
reclassificada como sucesso.

## Gates de integração

A promoção deste ledger para `accepted` ainda precisa sobreviver aos gates de
integração sobre a árvore que contém esta própria decisão/evidência e, após o
merge, sobre a `main`. Esses gates não podem ser substituídos pelo run candidato
acima; se falharem, o merge/ativação oficial permanece bloqueado.
