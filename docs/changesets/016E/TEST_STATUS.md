# CS016E — Test Status

State: `in_progress`

## Validação local do pacote

- Python syntax/compile dos novos verificadores: executado;
- JSON parse dos novos ledgers: executado;
- negative self-tests de authorizer/root verifier: preparados para CI;
- live GitHub provenance: obrigatório em CI;
- repository protection: obrigatório e atualmente tratado como hard blocker de merge;
- product runtime tests: **não aplicável ao ChangeSet de governança**, e nenhum resultado de produto é inferido desta correção.

## Histórico de CI preservado

### Bootstrap PR run 31625323514 — failure

Head validado: `d81d2adef3d8555a356dbdbb3d5e228db8bc9416`.

Passaram antes da falha:

- D-Lab action authorization self-test;
- Governance root self-test;
- Verify candidate governance root.

A execução foi interrompida em `Verify live GitHub governance provenance` com `PR #23/#24/#25/#26: merge SHA mismatch`. A checagem de merge SHA não foi removida nem relaxada.

### Diagnostic run 31625568909 — failure

O diagnóstico expôs que a resposta obtida dentro do GitHub Actions fornecia `merge_commit_sha=null` para os PRs históricos #23–#26. Isso não foi tratado como sucesso. O verificador foi endurecido para, na ausência desse campo, exigir simultaneamente:

- resolução exata do merge commit esperado pela API;
- presença do accepted-state head como parent do merge commit;
- associação oficial do commit esperado ao PR correto via `/commits/{sha}/pulls`;
- PR fechado/merged e base `main`.

### Hardened provenance run 31625679559 — failure posterior

Head validado: `13663a2267f994a9031e4e1124dac88c7541d812`.

Passaram:

- D-Lab action authorization self-test;
- Governance root self-test;
- Verify candidate governance root;
- **Verify live GitHub governance provenance**.

A execução então falhou em `D-Lab governance self-test` com:

`lifecycle fixture: in_progress start_stage was not AUTHORIZED`

Causa: o verificador CS016D aceito modelava lifecycle sintético sem ACTION_SCOPE porque o authorizer anterior não exigia scope em `start_stage`; o authorizer endurecido de CS016E passou corretamente a exigir um scope válido. O teste histórico não será enfraquecido nem reescrito. CS016E adiciona `verify_dlab_governance_v15.py`, que primeiro reexecuta integralmente o verificador/self-test CS016D no snapshot aceito `de55e0882c6400a0409b5cf881c6ee796a975cdf` e, só depois, executa fixtures v1.5 explícitos com ACTION_SCOPE válido.

D-Lab corrente, evolution verifier, produto, manifest e proteção não foram executados após essa falha por fail-fast.

## Evidência ainda ausente

CS016E ainda não possui:

- qualifying source/run final;
- accepted-state source/run;
- PR merge SHA;
- successful post-merge trusted-root run;
- evidence manifest de aceitação;
- entrada CS016E na acceptance chain.

A ausência desses itens significa **não aceito**, não aprovação provisória.

## Boundary de CS017

CS017 R4 é preservado como não qualificante. Nenhum `stage_operation` ou campanha qualificante EV-00 deve ocorrer enquanto CS016E não estiver integralmente fechado.
