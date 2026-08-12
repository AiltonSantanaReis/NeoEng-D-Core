# NeoEng D-Core — Plano Mestre Pós-v1.14.1 — Amendment 1.2

Documento ID: `NEOENG-DCORE-EVOLUTION-001-A2`  
Programa: `POST_1_14_1`  
Baseline histórica protegida: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Versão normativa efetiva: **1.2**

## 1. Natureza append-only

Este documento complementa, sem reescrever:

1. `POST_1_14_1_EVOLUTION_MASTER_PLAN.md` — versão 1.0;
2. `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md` — versão 1.1;
3. `DLAB_VALIDATION_STANDARD.md`.

Nenhuma regra anterior é relaxada. Em caso de conflito material, aplica-se a precedência registrada em `audit/SOURCE_OF_TRUTH_INDEX.json` e o trabalho permanece bloqueado até reconciliação.

Este amendment existe porque o fluxo normativo de desvio exige:

`STOP -> deviation record -> impact analysis -> amendment -> verification -> resume`

Ele não foi criado para converter uma falha em aprovação. Ele formaliza uma correção do mecanismo de self-test que deve continuar rejeitando ações inválidas.

## 2. Desvio que originou CS016B

Desvio: `DEV-0002`.

Após CS016A ser aceito, a preparação inicial de CS017 criou o candidato:

`bfafa432ad4dc7c402753293da080fc6d920c8ce`

e promoveu operacionalmente EV-00 de `not_started` para `in_progress`, conforme o fluxo de início de ChangeSet.

O workflow `31594048822` bloqueou antes de qualquer campanha de produto com:

`EV-00 preflight remained rejected after simulated amendment acceptance: preflight requires stage status not_started; actual='in_progress'`

A regra operacional estava correta: uma nova preparação deve ser rejeitada quando o stage já está `in_progress`.

O defeito estava no **self-test**: ele construía fixtures explícitos para o status de CS016A, mas herdava implicitamente o lifecycle state real de EV-00 para uma asserção sintética que pretendia representar o estado `not_started`.

## 3. Causa raiz

A correção anterior do incidente de CS016A tornou o self-test independente do lifecycle do amendment, porém não do lifecycle do stage.

Havia, portanto, duas dimensões distintas:

- lifecycle do amendment;
- lifecycle do stage.

A primeira foi isolada por fixture explícito; a segunda permaneceu acoplada ao `audit/EVOLUTION_ROADMAP.json` real.

Esse acoplamento faz um self-test válido em `EV-00:not_started` tornar-se inválido por razões artificiais quando o repositório avança legitimamente para `EV-00:in_progress`.

## 4. Regra preservada — sem relaxar gate

As regras existentes permanecem:

- `prepare_stage_changeset` e `preflight` exigem stage `not_started`;
- `start_stage` exige stage `in_progress`;
- `stage_operation` exige stage `in_progress`, `ACTION_SCOPE` e allowlist;
- todos os amendments requeridos pelo stage devem estar `accepted`;
- `REJECT` continua bloqueante;
- ausência ou divergência continua fail-closed.

CS016B **não altera** essas decisões.

## 5. Nova obrigação de self-test

Todo self-test sintético do Action Authorization Gate que dependa de lifecycle de stage deve construir explicitamente o estado que pretende provar.

Para EV-00/CS017, o conjunto mínimo permanente é:

1. fixture `EV-00:not_started` + amendments aceitos:
   - `prepare_stage_changeset(CS017)` => `AUTHORIZED`;
2. fixture `EV-00:in_progress` + amendments aceitos:
   - `prepare_stage_changeset(CS017)` => `REJECT`;
   - `start_stage(CS017)` => `AUTHORIZED`;
3. amendment obrigatório não aceito:
   - preparação do stage => `REJECT` com blocker identificado;
4. amendment já aceito:
   - novo `governance_amendment` para o mesmo ChangeSet => `REJECT`.

O estado real do repositório pode ser usado para verificar a operação corrente, mas nunca como precondição implícita de um cenário sintético que modele outro estado.

## 6. Regressão permanente

Novo cenário obrigatório:

`SCN-REGRESSION-002`

Objetivo: reproduzir o defeito de lifecycle descoberto no run `31594048822`.

Oracle:

- fixture explícito `not_started` autoriza preparação;
- fixture explícito `in_progress` rejeita re-preparação;
- fixture explícito `in_progress` autoriza `start_stage`;
- o self-test termina `ACCEPT` independentemente de o repositório real estar em `not_started` ou `in_progress`, desde que o estado real em si seja normativamente válido.

A regressão não testa runtime e não cria claim de produto.

## 7. Requisito suplementar

### EVREQ-072

Action Authorization self-tests devem ser lifecycle-independent por fixtures explícitos e preservar simultaneamente:

- autorização correta em `not_started`;
- rejeição correta de re-preparação em `in_progress`;
- autorização correta de `start_stage` em `in_progress`;
- blockers de amendments obrigatórios.

Ledger: `audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016B.json`.

## 8. Invariante suplementar

### INV-EV-028

Self-test sintético de governança não pode herdar implicitamente o lifecycle state real que pretende simular.

Enforcement:

- `scripts/authorize_evolution_action.py --self-test`;
- `scripts/verify_dlab_governance.py --self-test`;
- `SCN-REGRESSION-002`;
- workflow de governança.

Ledger: `audit/EVOLUTION_INVARIANTS_AMENDMENT_016B.json`.

## 9. Evolução do verificador do D-Lab

O verificador do D-Lab deve deixar de assumir que existe exatamente um amendment pós-CS016.

Ele passa a validar, append-only:

- CS016A como predecessor obrigatório e já aceito;
- CS016B como correção atual;
- ordem dos amendments;
- documentos, ledgers, scopes e evidências de cada amendment;
- regressões `SCN-REGRESSION-001` e `SCN-REGRESSION-002`;
- comportamento do autorizador no lifecycle corrente.

Nenhuma verificação existente de baseline, histórico, cenário, oracle, seed, source-under-test, evidência, manifesto, fail-closed ou allowlist é removida.

## 10. Gate de workflow

O gate fixo “CS016A authorization-state gate” é generalizado para “Required evolution amendments gate”.

Ele deve:

- enumerar amendments requeridos antes de EV-00;
- se algum estiver `in_progress`, autorizar apenas o trabalho desse amendment e exigir que preparação de CS017 seja rejeitada;
- se todos estiverem `accepted` e EV-00 estiver `not_started`, exigir que preparação de CS017 seja autorizada;
- se todos estiverem `accepted` e EV-00 estiver `in_progress`, exigir que nova preparação seja rejeitada e `start_stage` seja autorizada;
- rejeitar qualquer outro estado não contemplado.

A generalização não reduz gates; ela elimina uma premissa hardcoded e amplia a cobertura.

## 11. Relação com CS017

Enquanto CS016B estiver diferente de `accepted`:

- CS017 permanece interrompido;
- a branch que produziu o run `31594048822` é evidência histórica de falha;
- nenhum resultado daquela tentativa é promovido a campanha qualificante de EV-00;
- EV-00 oficial em `main` permanece `not_started`.

Após CS016B ser aceito e incorporado à `main`, CS017 deve ser retomado a partir da nova `main` e reavaliar o Action Authorization Gate. A branch falha não é tratada como se suas precondições permanecessem válidas.

## 12. Impact analysis

### Runtime / ABI / dados canônicos

Sem impacto.

Não há alteração de:

- `src/`;
- `include/`;
- ABI;
- Host SDK;
- replay;
- rollback;
- snapshot;
- serialização;
- hashing canônico;
- transições;
- formato persistente.

### Claims

Sem expansão ou promoção.

### Releases

Nenhuma release é autorizada.

### Histórico

A baseline `v1.14.1` e as evidências anteriores permanecem imutáveis.

### Testes

Nenhum teste é removido, enfraquecido, encurtado ou reclassificado para obter PASS. O self-test recebe cobertura adicional de lifecycle.

## 13. ChangeSet corretivo

ChangeSet: `CS016B`.

Escopo permitido exclusivamente de governança:

- novo amendment 1.2;
- `DEV-0002`;
- ledgers suplementares de CS016B;
- registro append-only no ledger de amendments;
- `SCN-REGRESSION-002`;
- correção do fixture do Action Authorization self-test;
- generalização conservadora do D-Lab verifier;
- generalização do workflow de amendment state;
- Source of Truth Index para registrar os novos artefatos;
- evidência e manifesto do próprio CS016B.

Produto e planos anteriores permanecem fora do write scope.

## 14. Critério de aceitação de CS016B

CS016B só pode ser `accepted` quando:

1. `scripts/authorize_evolution_action.py --self-test` passa;
2. o self-test prova explicitamente os estados `not_started` e `in_progress`;
3. `SCN-REGRESSION-002` está `passed` com evidência;
4. `EVREQ-072` está `verified` com evidência;
5. `INV-EV-028` permanece ativo;
6. `scripts/verify_dlab_governance.py --self-test` passa;
7. `scripts/verify_dlab_governance.py` passa;
8. `scripts/verify_evolution_plan.py --self-test` passa;
9. `scripts/verify_evolution_plan.py` passa;
10. product contract e product assurance continuam passando;
11. manifesto verifica;
12. evidência de falha do run `31594048822` permanece preservada;
13. candidato corrigido possui source SHA e evidence manifest;
14. PR passa no mesmo head;
15. `main` passa novamente após merge.

Falha em qualquer gate mantém CS016B não aceito e CS017 interrompido.
