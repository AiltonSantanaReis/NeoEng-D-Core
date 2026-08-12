# ChangeSet 016 — status de validação

State: in_progress

## Estado

O bootstrap normativo foi definido, porém ainda não está aceito.

Enquanto este estado persistir:

- `program_state = locked_pending_bootstrap_acceptance`;
- `current_stage = null`;
- EV-00..EV-20 permanecem `not_started`;
- `release_authorized = false`.

## Gates

| Gate | Estado | Evidência |
|---|---|---|
| Estrutura normativa | NOT_TESTED | aguardando SHA candidato |
| Self-test fail-closed | NOT_TESTED | aguardando workflow |
| Evolution verifier | NOT_TESTED | aguardando workflow |
| Product contract verifier | NOT_TESTED | aguardando workflow |
| Product assurance verifier | NOT_TESTED | aguardando workflow |
| Manifest | NOT_TESTED | aguardando manifesto final |
| GitHub Actions | NOT_TESTED | aguardando run candidato |
| Evidence manifest | NOT_TESTED | aguardando registro da campanha |

Nenhum estado `NOT_TESTED` acima equivale a aprovação.
