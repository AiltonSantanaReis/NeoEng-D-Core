# Índice de estado documental

Documento de reconciliação para a baseline aceita `1.14.0`.

Este índice separa documentos normativos atuais, registros operacionais e
artefatos históricos. Ele não cria claims, não fecha limitações e não substitui
os ledgers de máquina.

## Documentos que determinam o estado atual

- [`NEOENG_DCORE_SOURCE_OF_TRUTH.md`](NEOENG_DCORE_SOURCE_OF_TRUTH.md): fonte
  normativa de precedência máxima;
- [`audit/SOURCE_OF_TRUTH_INDEX.json`](../../audit/SOURCE_OF_TRUTH_INDEX.json):
  índice de precedência e verificadores;
- [`audit/FINAL_ACCEPTANCE_VALIDATION.json`](../../audit/FINAL_ACCEPTANCE_VALIDATION.json):
  estado atual da aceitação CS015;
- [`PRODUCT_CLOSURE_PLAN.md`](PRODUCT_CLOSURE_PLAN.md): plano de fechamento;
- [`PRODUCT_COMPLETION_STANDARD.md`](PRODUCT_COMPLETION_STANDARD.md): critérios
  normativos de conclusão;
- [`PRODUCT_CLAIMS_LEDGER.json`](../../audit/PRODUCT_CLAIMS_LEDGER.json) e
  [`PUBLIC_CLAIMS.md`](../commercial/PUBLIC_CLAIMS.md): claims autorizadas e
  seus limites;
- [`DEFERRED_VALIDATION_GATES.json`](../../audit/DEFERRED_VALIDATION_GATES.json):
  gates nativos, externos e de infraestrutura que continuam deferidos.

## Registros operacionais atuais

- [`docs/changesets/014/TEST_STATUS.md`](../changesets/014/TEST_STATUS.md):
  assurance da release `1.14.0`;
- [`docs/changesets/015/TEST_STATUS.md`](../changesets/015/TEST_STATUS.md):
  aceitação horizontal CS015;
- [`docs/records/CS001_R9_GOVERNANCE_ENVIRONMENT_CLOSURE.md`](../records/CS001_R9_GOVERNANCE_ENVIRONMENT_CLOSURE.md):
  reconciliação de governança, ambiente e proteção de novas releases.
- [`docs/records/POST_MERGE_REVALIDATION_20260810.md`](../records/POST_MERGE_REVALIDATION_20260810.md):
  revalidação local do merge corretivo, sem nova baseline ou release.

## Registros históricos preservados

[`docs/VALIDATION_REPORT.md`](../VALIDATION_REPORT.md),
[`docs/AUDIT_STATUS.md`](../AUDIT_STATUS.md), `PACKAGE_VALIDATION.json`,
`SOURCE_PROVENANCE.json` e os pacotes de evidência de cada ChangeSet preservam
recortes de execução anteriores. Campos como “pending”, “not executed” ou
`commercial_product_complete: false` nesses recortes não substituem o estado
final da baseline; devem ser interpretados com a data, a fonte e o ChangeSet
registrados.

Evidências imutáveis e saídas brutas não são reescritas para harmonizar texto.
Quando uma evidência de fechamento aparece como `closure_candidate`, ela é o
artefato pré-decisão; o estado final deve ser consultado no registro de
aceitação posterior.
