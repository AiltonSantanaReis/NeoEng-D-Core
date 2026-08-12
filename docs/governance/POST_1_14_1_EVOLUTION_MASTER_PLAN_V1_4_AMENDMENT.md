# NeoEng D-Core — Plano Mestre Pós-v1.14.1 — Amendment 1.4

Documento ID: `NEOENG-DCORE-EVOLUTION-001-A4`  
Programa: `POST_1_14_1`  
Baseline histórica protegida: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Versão normativa efetiva: **1.4**

## 1. Natureza append-only

Este documento complementa, sem reescrever ou relaxar:

1. `POST_1_14_1_EVOLUTION_MASTER_PLAN.md` — versão 1.0;
2. `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md` — versão 1.1;
3. `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md` — versão 1.2;
4. `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_3_AMENDMENT.md` — versão 1.3;
5. `DLAB_VALIDATION_STANDARD.md` — versão 1.0.

Permanece obrigatório:

`STOP -> deviation record -> impact analysis -> amendment -> verification -> resume`

Nenhum teste, gate, oracle, requirement, invariant, claim ou critério anterior é
removido ou enfraquecido.

## 2. Desvio que originou CS016D

Desvio: `DEV-0004`.

Após CS016C ser aceito, mergeado e validado em `main` no commit
`7393b32d2be3fd2e65eab6a738a0066c13848f6c`, CS017 foi reconstruído como R3.
O post-merge gate da `main` havia retornado:

- `EV-00:not_started`;
- CS016A/B/C `accepted`;
- `prepare_stage_changeset(CS017) => AUTHORIZED`.

A preparação R3 criou o commit
`5c5f328afc919527775742c702e3d1a47c1490c9`, cuja única mudança operacional de
stage foi `EV-00: not_started -> in_progress`.

O workflow `31613924661`, job `94171862183`, bloqueou no D-Lab governance
self-test com:

`protected v1.2 file changed during CS016C: audit/EVOLUTION_ROADMAP.json`

O Action Authorization self-test havia passado. Nenhum harness EV-00,
`stage_operation`, build da baseline ou campanha qualificante de produto foi
executado.

## 3. Causa raiz

Classificação: **governança / verifier lifecycle scope**.

O verifier 1.3 criado por CS016C foi corretamente conservador durante a
aceitação de CS016C, porém materializou uma condição temporal como se fosse
invariante permanente: incluiu ledgers operacionais, em especial
`audit/EVOLUTION_ROADMAP.json`, em uma comparação byte a byte eterna contra o
snapshot pré-CS016C `855ff456...`.

Isso conflita com o próprio lifecycle normativo:

- o roadmap é a fonte operacional de estado;
- `prepare_stage_changeset` existe para promover o stage de `not_started` para
  `in_progress` em uma branch de ChangeSet;
- Amendment 1.3 exige reconstruir CS017 após CS016C e solicitar novamente
  `prepare_stage_changeset`, `start_stage` e `stage_operation`.

Logo, o erro não é a alteração do roadmap. O erro é o verifier confundir
**imutabilidade histórica/normativa** com **imutabilidade eterna de estado
operacional**.

## 4. Duas classes explícitas de artefatos

### 4.1 Imutáveis após aceitação

Devem permanecer byte a byte preservados, salvo novo amendment explicitamente
superior que declare substituição sem reescrita retroativa:

- Source of Truth histórico aplicável;
- Planos Mestre e Amendments já aceitos;
- D-Lab Validation Standard vigente;
- ChangeSets corretivos aceitos;
- TEST_STATUS aceitos;
- deviation records fechados;
- evidence manifests e evidência aceita de ChangeSets anteriores.

### 4.2 Operacionais mutáveis sob autorização

Podem mudar somente quando o lifecycle/ChangeSet atual e o Action Authorization
Gate autorizarem a mudança:

- `audit/EVOLUTION_ROADMAP.json`;
- requirement ledgers com status/evidence de stages;
- historical revalidation matrix;
- scenario catalog;
- amendment ledger por append-only;
- Source of Truth Index por registro append-only de novos artefatos;
- D-Lab execution policy quando um amendment autorizado acrescentar enforcement.

Ser mutável não significa livremente editável. Toda mudança continua limitada
por ChangeSet, allowlist, lifecycle, verifiers e evidência.

## 5. Preservação do gate 1.3

O verifier 1.4 deve reexecutar o verifier 1.3 e seu self-test no snapshot oficial
aceito pós-CS016C:

`7393b32d2be3fd2e65eab6a738a0066c13848f6c`

Essa reexecução prova a aceitação histórica de CS016C no estado em que ela
ocorreu.

Na árvore corrente, o verifier 1.4 deve:

1. provar que artefatos normativos/evidenciais já aceitos permanecem intactos;
2. validar semanticamente os ledgers operacionais atuais em vez de exigir
   igualdade byte a byte com um estado histórico;
3. validar que alterações operacionais correspondem ao lifecycle atual e aos
   amendments requeridos;
4. preservar regressões 001, 002 e 003;
5. rejeitar alteração não autorizada de artefato imutável.

## 6. Regressão permanente

Novo cenário obrigatório: `SCN-REGRESSION-004`.

Oracle mínimo:

- snapshot aceito 1.3 continua verificável;
- fixture/current state `EV-00:not_started` com amendments aceitos é válido;
- lifecycle autorizado `EV-00:in_progress` não falha apenas porque o roadmap
  difere do snapshot histórico;
- re-preparação em `in_progress` continua `REJECT`;
- `start_stage` em `in_progress` continua `AUTHORIZED`;
- alteração de um artefato normativo/aceito imutável continua `REJECT`;
- `release_authorized=false` continua preservado.

A regressão qualifica governança/verifier, não runtime de produto.

## 7. Requisito suplementar

### EVREQ-074

O D-Lab governance verifier deve distinguir artefatos aceitos imutáveis de
ledgers operacionais mutáveis, revalidar snapshots históricos no SHA em que
foram aceitos e permitir somente transições operacionais autorizadas pelo
lifecycle atual, sem congelar permanentemente o roadmap ou ledgers de stage.

Ledger: `audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016D.json`.

## 8. Invariante suplementar

### INV-EV-030

Verificação de imutabilidade histórica não pode impedir uma transição de
lifecycle explicitamente autorizada, e uma transição operacional autorizada não
pode enfraquecer a imutabilidade de documentos/evidências já aceitos.

Enforcement:

- `scripts/verify_dlab_governance.py --self-test`;
- `scripts/verify_dlab_governance.py`;
- `scripts/authorize_evolution_action.py --self-test`;
- `SCN-REGRESSION-004`;
- workflow de governança.

Ledger: `audit/EVOLUTION_INVARIANTS_AMENDMENT_016D.json`.

## 9. Relação com CS017

Enquanto CS016D estiver diferente de `accepted`:

- CS017 permanece interrompido;
- a branch R3 e o run `31613924661` permanecem evidência de falha de governança;
- nenhum resultado R3 qualifica EV-00;
- EV-00 oficial em `main` permanece `not_started`;
- nenhum `stage_operation` ou campanha de produto é executado.

Após CS016D ser aceito, mergeado e validado em `main`, CS017 deve ser novamente
reconstruído a partir dessa nova `main`; a branch R3 não é retomada.

## 10. Impact analysis

### Runtime / ABI / estado canônico

Nenhum impacto autorizado. Produto, Host SDK, replay, rollback, snapshots,
serialização, hashes, transições e formatos persistentes permanecem fora do
write scope.

### Claims

Nenhuma expansão.

### Release

Nenhuma release é autorizada. `release_authorized` permanece `false`.

### Governança

A correção remove apenas a falsa premissa de que um ledger operacional aceito em
um instante deva permanecer byte a byte congelado em todos os ChangeSets
futuros. Gates de lifecycle, allowlists, evidência, manifests e regressões
continuam obrigatórios.

## 11. ChangeSet corretivo

ChangeSet: `CS016D`.

Escopo exclusivo de governança:

- este Amendment 1.4;
- `DEV-0004`;
- ledgers suplementares EVREQ-074/INV-EV-030;
- registro append-only no amendments ledger;
- SCN-REGRESSION-004 e binding de policy;
- generalização lifecycle-aware do D-Lab governance verifier;
- Source of Truth Index apenas para registrar a camada 1.4;
- evidência e manifesto de CS016D.

Planos 1.0/1.1/1.2/1.3 e D-Lab Standard permanecem read-only.
