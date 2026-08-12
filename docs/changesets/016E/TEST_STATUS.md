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

Causa: o verificador CS016D aceito modelava lifecycle sintético sem ACTION_SCOPE porque o authorizer anterior não exigia scope em `start_stage`; o authorizer endurecido de CS016E passou corretamente a exigir um scope válido. O teste histórico não foi enfraquecido nem reescrito. CS016E adicionou `verify_dlab_governance_v15.py`, que primeiro reexecuta integralmente o verificador/self-test CS016D no snapshot aceito `de55e0882c6400a0409b5cf881c6ee796a975cdf` e, só depois, executa fixtures v1.5 explícitos com ACTION_SCOPE válido.

### PR run 31626266023 — root drift rejected

Ao tornar `verify_dlab_governance_v15.py` root-critical, o global forbidden set passou a exigi-lo. O máximo EV-00 ainda não o continha e `Verify candidate governance root` rejeitou:

`stage maximum omits global governance forbidden paths EV-00: scripts/verify_dlab_governance_v15.py`

A correção propagou a proibição ao `STAGE_SCOPE_MAXIMA`; nenhum forbidden foi removido.

### Push run 31626332281 — normative binding rejected

Depois da propagação, root e live provenance passaram. O D-Lab v1.5 self-test rejeitou porque o Amendment 1.5 ainda não continha literalmente o snapshot 1.4 que o ratchet reexecuta:

`Amendment 1.5 missing token: de55e0882c6400a0409b5cf881c6ee796a975cdf`

O Amendment foi fortalecido para declarar explicitamente que o verifier/self-test CS016D é reexecutado nesse SHA e que uma falha histórica não pode ser compensada por testes novos.

### Push run 31626425222 — logical gates passed; manifest stale

Head validado: `c1ef8adedc07d493f16bf4f15db4096743f94906`.

Passaram integralmente antes do manifest:

- D-Lab action authorization self-test;
- Governance root self-test;
- Verify candidate governance root;
- Verify live GitHub governance provenance;
- D-Lab governance v1.5 self-test;
- Verify D-Lab governance v1.5;
- Required evolution amendments gate;
- Evolution verifier self-test;
- Verify evolution plan;
- Verify product contract;
- Verify product assurance.

A única falha foi `Verify tracked-file manifest`, como esperado enquanto os novos arquivos ainda não estavam reconciliados. O manifest não será alterado manualmente; o job `Controlled manifest reconciliation` deve gerar e commitar somente `MANIFEST.sha256`.

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
