# Índice de estado documental

Documento de reconciliação para a **release aceita `v1.14.1`** e para o estado pós-release observado em `main` durante esta modernização.

Este índice separa documentos normativos atuais, registros operacionais, documentação explicativa e artefatos históricos. Ele não cria claims, não fecha limitações, não inicia estágio evolutivo e não substitui ledgers de máquina.

## Estado de referência

- release histórica aceita: `v1.14.1`;
- commit da release: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
- base Git usada por esta modernização: `d092ac56290d76dddf51982549a98234f038f3ee`;
- essa base posterior à release registra mudança administrativa de governança e declara nenhuma alteração de produto/runtime/ABI;
- programa pós-1.14.1: `POST_1_14_1`, estado `active`;
- roadmap: `EV-00` é o estágio corrente, permanece `not_started`, com `CS017` planejado;
- `release_authorized` permanece `false`.

O estado de máquina prevalece sobre esta síntese.

## Documentos que determinam o estado atual

- [`NEOENG_DCORE_SOURCE_OF_TRUTH.md`](NEOENG_DCORE_SOURCE_OF_TRUTH.md): fonte normativa de precedência máxima;
- [`audit/SOURCE_OF_TRUTH_INDEX.json`](../../audit/SOURCE_OF_TRUTH_INDEX.json): índice de precedência, ledgers e verificadores;
- [`audit/FINAL_ACCEPTANCE_VALIDATION.json`](../../audit/FINAL_ACCEPTANCE_VALIDATION.json): aceitação da baseline aplicável;
- [`POST_1_14_1_EVOLUTION_MASTER_PLAN.md`](POST_1_14_1_EVOLUTION_MASTER_PLAN.md): plano mestre pós-release;
- [`POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md`](POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md): amendment efetivo apontado pelo Source of Truth Index;
- [`audit/EVOLUTION_ROADMAP.json`](../../audit/EVOLUTION_ROADMAP.json): estado operacional dos estágios;
- [`PRODUCT_COMPLETION_STANDARD.md`](PRODUCT_COMPLETION_STANDARD.md): critérios normativos de conclusão;
- [`PRODUCT_ASSURANCE_TEST_STANDARD.md`](PRODUCT_ASSURANCE_TEST_STANDARD.md): assurance;
- [`audit/PRODUCT_CLAIMS_LEDGER.json`](../../audit/PRODUCT_CLAIMS_LEDGER.json) e [`PUBLIC_CLAIMS.md`](../commercial/PUBLIC_CLAIMS.md): claims autorizadas e seus limites;
- [`audit/DEFERRED_VALIDATION_GATES.json`](../../audit/DEFERRED_VALIDATION_GATES.json): gates que continuam fora de claims já aceitas.

## Estado da validação de ChangeSets

A política corrente legível por máquina está em:

- [`audit/CHANGESET_VALIDATION_POLICY.json`](../../audit/CHANGESET_VALIDATION_POLICY.json);
- [`audit/CURRENT_CHANGESET_VALIDATION.json`](../../audit/CURRENT_CHANGESET_VALIDATION.json).

Na `main` de base `d092ac56290d76dddf51982549a98234f038f3ee`, o descriptor preservado aponta para `CS000A`, fechamento administrativo que tornou os workflows legados CS016 manuais.

Nesta branch de modernização documental, `CS000B` é ativado prospectivamente apenas como ChangeSet administrativo/documental de validação da candidata. O plano fica em [`../../audit/validation/CS000B/VALIDATION_PLAN.json`](../../audit/validation/CS000B/VALIDATION_PLAN.json). Essa ativação de branch não inicia `EV-00`, não inicia nem aceita `CS017`, não altera runtime/ABI e não autoriza nova release.

## Registros operacionais e de release

- [`docs/changesets/014/TEST_STATUS.md`](../changesets/014/TEST_STATUS.md): release assurance e histórico de 1.14.0/1.14.1;
- [`docs/changesets/015/TEST_STATUS.md`](../changesets/015/TEST_STATUS.md): aceitação horizontal e evidência de 1.14.1;
- [`docs/changesets/016/TEST_STATUS.md`](../changesets/016/TEST_STATUS.md): bootstrap do programa pós-1.14.1;
- [`docs/records/POST_MERGE_REVALIDATION_20260810.md`](../records/POST_MERGE_REVALIDATION_20260810.md): revalidação registrada, sem promoção automática de nova baseline.

## Documentação explicativa atual

Os arquivos abaixo melhoram navegação e interpretação, mas não têm precedência sobre os documentos normativos:

- [`../README.md`](../README.md) — portal de documentação;
- [`../PROJECT_STATUS.md`](../PROJECT_STATUS.md) — status executivo;
- [`../ARCHITECTURE_OVERVIEW.md`](../ARCHITECTURE_OVERVIEW.md) — visão arquitetural;
- [`../INTEGRATION_GUIDE.md`](../INTEGRATION_GUIDE.md) — integração;
- [`../USER_GUIDE_PT-BR.md`](../USER_GUIDE_PT-BR.md) — guia detalhado;
- [`../RESULTS_AND_CLAIMS_GUIDE.md`](../RESULTS_AND_CLAIMS_GUIDE.md) — interpretação de resultados;
- [`../TROUBLESHOOTING.md`](../TROUBLESHOOTING.md) — diagnóstico;
- [`../SECURITY_AND_TRUST_BOUNDARIES.md`](../SECURITY_AND_TRUST_BOUNDARIES.md) — segurança e limites de confiança.

## Registros históricos preservados

[`../VALIDATION_REPORT.md`](../VALIDATION_REPORT.md), `PACKAGE_VALIDATION.json`, `SOURCE_PROVENANCE.json`, `MANIFEST.sha256`, ChangeSets e seus pacotes de evidência preservam recortes anteriores.

Campos históricos como `pending`, `not executed`, `closure_candidate`, `commercial_product_complete: false` ou estados intermediários devem ser interpretados com data, fonte, commit e ChangeSet correspondentes.

Evidências brutas não são reescritas apenas para harmonizar documentação nova.

## Regra release versus desenvolvimento pós-release

Uma árvore posterior a `v1.14.1` não deve ser atribuída retroativamente ao pacote publicado. Mudanças pós-release precisam de identidade e evidência próprias antes de qualquer promoção de release ou claim.

## D-Core versus D-Lab

Documentos do D-Core podem especificar requisitos para validação por D-Lab externo. Isso não transforma o D-Lab em parte do produto e não transfere automaticamente verdicts ou governance state entre repositórios.

Quando houver conflito, use a autoridade do domínio correspondente e exija provenance explícita para qualquer evidência importada.
