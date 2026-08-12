# ChangeSet 016 — bootstrap da governança de evolução pós-1.14.1

Baseline histórica protegida: `v1.14.1`  
Baseline SHA: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Branch de trabalho: `agent/evolution-governance-foundation`  
State: accepted

## Objetivo

Instalar o sistema normativo de três níveis que governará a evolução do
NeoEng D-Core após a baseline 1.14.1:

1. documento normativo humano;
2. ledgers legíveis por máquina;
3. ChangeSets e evidências imutáveis.

CS016 é um bootstrap de governança. Ele não é uma etapa técnica EV-* e não
autoriza iniciar EV-00 antes de sua própria aceitação.

## Escopo autorizado

- adicionar `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md`;
- adicionar `audit/EVOLUTION_ROADMAP.json`;
- adicionar `audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json`;
- adicionar `audit/EVOLUTION_INVARIANTS.json`;
- adicionar `scripts/verify_evolution_plan.py`;
- registrar os novos documentos/ledgers/verificador em
  `audit/SOURCE_OF_TRUTH_INDEX.json`;
- adicionar workflow de validação automática da governança;
- criar este ChangeSet, status de testes e evidência correspondente;
- atualizar `MANIFEST.sha256` para a árvore final do ChangeSet.

## Não objetivos

- nenhum código do núcleo determinístico será alterado;
- nenhuma ABI C será alterada;
- nenhum formato de replay, snapshot ou evidência de runtime será alterado;
- nenhuma claim pública será ampliada;
- nenhuma release será criada;
- nenhuma etapa EV-00..EV-20 será iniciada;
- a fonte normativa primária existente não será reescrita se a autoridade já
  puder ser estabelecida pelo índice normativo vigente.

## Justificativa para não editar a fonte primária

`NEOENG_DCORE_SOURCE_OF_TRUTH.md` já estabelece que ele e os registros
listados em `audit/SOURCE_OF_TRUTH_INDEX.json` constituem a fonte normativa e
que conflitos com material de menor precedência bloqueiam o trabalho.

CS016 utiliza esse mecanismo existente: registra explicitamente o plano,
ledgers e verificador na hierarquia do índice, sem modificar retroativamente a
fonte histórica desnecessariamente.

## Invariantes

CS016 deve preservar integralmente:

- autoridade canônica atual;
- comportamento de `WorldState v1`;
- determinismo;
- replay/rollback;
- Host SDK;
- artefatos e evidência da v1.14.1;
- claims e limitações atuais;
- políticas de release e final acceptance.

## Gate de entrada

- `main` identificada em
  `7dbb117c107b6491e2b313333568b9155c4847ea`;
- release histórica `v1.14.1` identificada em
  `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
- CS016 inexistente antes deste ChangeSet.

## Testes obrigatórios

1. `python3 scripts/verify_evolution_plan.py --self-test`;
2. `python3 scripts/verify_evolution_plan.py`;
3. `python3 scripts/verify_product_contract.py`;
4. `python3 scripts/verify_product_assurance.py`;
5. `python3 scripts/generate_manifest.py --check`;
6. validação do workflow no SHA candidato;
7. inspeção do manifesto de evidência do CS016.

## Testes negativos mínimos do verificador

O self-test deve rejeitar, no mínimo:

- etapa iniciada antes da dependência;
- duas etapas `in_progress`;
- release autorizada antes do fechamento;
- etapa `accepted` sem evidência;
- requisito apontando para etapa inexistente;
- invariante obrigatório ausente;
- verificador não registrado no índice;
- bootstrap aceito sem evidência;
- evidência adulterada após manifestada.

## Critérios de saída

CS016 pode ser aceito somente quando:

- documentos e ledgers são coerentes;
- EV-00..EV-20 existem uma única vez e em ordem;
- EV-00..EV-20 permanecem `not_started` no SHA candidato;
- `program_state` permanece bloqueado no SHA candidato;
- self-test fail-closed passa;
- verificadores de produto continuam passando;
- manifesto da árvore confere;
- workflow executa com sucesso no SHA candidato;
- evidência do run é registrada sob `docs/changesets/016/evidence/`;
- o ledger é promovido para bootstrap `accepted` somente após a evidência;
- `current_stage` então passa a `EV-00`, sem iniciar EV-00;
- `release_authorized` permanece `false`.

## Regra de falha

Qualquer divergência entre Markdown, ledgers, ChangeSet, manifesto, SHA ou
evidência bloqueia CS016. Não é permitido marcar o ChangeSet como aceito por
inspeção visual ou por resultado parcial.

## Registro de aceitação

O candidato reconciliado no source commit
`f8884c463cdcf11720f5abc359034c65d8c0c8a9` foi validado pelo workflow
`Evolution governance` no run `31557764638`, evento `push`, conclusão `success`.

A campanha registrou:

- self-test fail-closed: PASS;
- evolution plan verifier: PASS;
- product contract verifier: PASS;
- product assurance verifier: PASS;
- `MANIFEST.sha256`: PASS;
- artifact de evidência: `9126581075`;
- digest do artifact: `sha256:146493a9c39994510fe00d8313fa21bf28d7c370e127340221c28abd24f0ba65`.

A evidência imutável do ChangeSet está indexada por
`docs/changesets/016/evidence/EVIDENCE_MANIFEST.json`.

A aceitação ativa o programa de evolução e seleciona `EV-00` como etapa
corrente administrativa, porém **EV-00 permanece `not_started`**. Nenhuma
implementação técnica EV-* é inferida, nenhuma release é autorizada e nenhum
claim do produto é ampliado por CS016.
