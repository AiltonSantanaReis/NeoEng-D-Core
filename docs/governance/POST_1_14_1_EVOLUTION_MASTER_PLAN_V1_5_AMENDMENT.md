# NeoEng D-Core — Plano Mestre Pós-v1.14.1 — Amendment 1.5

Documento ID: `NEOENG-DCORE-EVOLUTION-001-A5`  
Programa: `POST_1_14_1`  
Baseline histórica protegida: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Versão normativa efetiva: **1.5**

## 1. Natureza e precedência

Este Amendment é **append-only** e complementa, sem reescrever, apagar, reclassificar ou enfraquecer o Source of Truth, Plano Mestre 1.0, Amendments 1.1–1.4, D-Lab Validation Standard, evidências, falhas e ChangeSets já aceitos. Permanece obrigatório `STOP -> deviation record -> impact analysis -> amendment -> verification -> resume`. Nenhum `FAILED`, `BLOCKED`, `ABORTED`, `NOT TESTED`, evidência ausente ou ambiente indisponível pode ser convertido em aprovação.

## 2. Desvio DEV-0005

A auditoria pós-CS016D identificou risco de autorreferência de governança: ações de escrita podiam omitir paths; o diff lia base/allowlist do próprio ACTION_SCOPE candidato; o scope podia tentar ampliar a própria autoridade; evidência local não provava sozinha provenance oficial; a cadeia qualifying/accepted/PR/merge/post-merge não era uma estrutura hash-chain única; advance/release eram menos autocontidos que o contrato normativo; `main` estava sem proteção externa; faltava máximo independente para stages futuros; e faltava um juiz vindo da base protegida. Esses pontos são risco prospectivo, não prova de fraude histórica; não foi encontrada evidência de falso resultado aprovado em CS016A/B/C/D.

## 3. Regra de parada

CS017 R4 (`253859d285963a2519af78cd7a30ba2d69fad9a1`, run `31618330920`) fica preservado como tentativa interrompida após start-state. Não executou campanha qualificante EV-00 antes desta correção, não pode ser reutilizado e qualquer continuidade de EV-00 deve nascer de nova branch da `main` pós-CS016E.

## 4. Raiz de confiança

CS016E introduz `audit/GOVERNANCE_ROOT_OF_TRUST.json`. São obrigatórios: fail-closed; candidato não escolhe o base SHA do próprio diff; ACTION_SCOPE só restringe, nunca amplia, o máximo confiável; ações de escrita exigem paths concretos; scope de stage é selado após start; acceptance provenance é conferida contra GitHub; release exige artifact attestation criptográfica; e proteções da raiz funcionam como ratchet, nunca como relaxamento.

## 5. ACTION_SCOPE não é autoridade sobre si mesmo

Toda mudança deve satisfazer simultaneamente: (1) trusted maximum; (2) ACTION_SCOPE; (3) diff real Git entre base confiável e head. `control_base_commit` no scope é apenas documental; o base autoritativo vem do PR/base protegido ou da main confiável.

## 6. Paths explícitos

`governance_amendment` e `stage_operation` são ações de escrita e `paths=[]` é `REJECT`. Ações puramente decisórias não recebem paths de escrita. `prepare_stage_changeset` continua sendo decisão de preparação e pode ser consultado sem paths; se paths forem fornecidos, devem obedecer ao máximo de preparação.

## 7. Scope máximo por stage

`audit/STAGE_SCOPE_MAXIMA.json` é a autoridade superior por stage. EV-00 recebe máximo explícito; EV-01..EV-20 ficam `undefined_stage_scope_is_REJECT` até Amendment anterior aceito defini-los. Um máximo não pode usar wildcard global `*`/`**`, escrever a própria raiz/workflows/verificadores/Source of Truth/documentos normativos, nem ampliar um máximo já aceito.

## 8. Scope sealing

Após stage entrar `in_progress`, seu ACTION_SCOPE fica byte-imutável em relação ao commit de introdução. Alterá-lo exige STOP, deviation record e novo amendment; não existe abertura de scope durante execução.

## 9. Cadeia criptográfica de aceitação

`audit/GOVERNANCE_ACCEPTANCE_CHAIN.json` registra qualifying SHA/run, accepted-state SHA/run, PR, merge SHA, post-merge run, hash anterior e SHA-256 da entrada atual em canonical JSON. Entradas anteriores são imutáveis; nova aceitação apenas acrescenta uma entrada. A/B/C/D são bootstrapados com dados reais auditados. E só pode ser anexado após todos os SHAs/runs reais e pós-merge validado.

## 10. Proveniência oficial

`verify_github_evidence.py` consulta a API oficial do GitHub e confirma run ID, head SHA, completed/success, workflow, repositório, PR/head, merge SHA, base main e post-merge run. JSON local + hashes continuam úteis para integridade, mas não bastam para provar existência de run oficial.

## 11. Trusted verifier ratchet

`.github/workflows/governance-root.yml` usa `pull_request_target`: verifier/root/maximums vêm da base `main`; candidato é apenas dado; nenhum script candidato é executado no job confiável. Portanto uma PR não troca o juiz e usa o juiz novo para autoaprovar a mesma mudança. CS016E é o bootstrap e não alega proteção retroativa; seu fechamento exige proteção externa e validação pós-merge.

A camada D-Lab v1.5 **não reescreve** o verificador aceito por CS016D. `scripts/verify_dlab_governance_v15.py` deve primeiro reexecutar `scripts/verify_dlab_governance.py` e seu self-test no snapshot oficial de governança v1.4 aceito:

`de55e0882c6400a0409b5cf881c6ee796a975cdf`

Somente após essa reexecução integral pode aplicar os novos fixtures e regras de CS016E. Uma falha do verifier 1.4 naquele snapshot bloqueia o verifier 1.5; não pode ser compensada por teste novo.

### 11.1 Selagem do próprio juiz e dos executores de governança

Após bootstrap tornam-se steady-state immutable: `.github/workflows/evolution-governance.yml`, `.github/workflows/governance-root.yml`, `audit/GOVERNANCE_ROOT_OF_TRUST.json`, `audit/REPOSITORY_PROTECTION_POLICY.json`, `scripts/authorize_evolution_action.py`, `scripts/verify_dlab_governance.py`, `scripts/verify_dlab_governance_v15.py`, `scripts/verify_governance_root.py`, `scripts/verify_governance_history.py`, `scripts/verify_github_evidence.py`, `scripts/verify_repository_protection.py` e `scripts/verify_release_attestation.py`. Amendment comum não pode modificá-los. Futuro defeito na própria raiz exige novo bootstrap de root-of-trust com STOP global, documento normativo superior, revisão externa e novo ratchet; a raiz substituída não pode autoaprovar a substituta.

### 11.2 Imutabilidade semântica da história aceita

`scripts/verify_governance_history.py`, executado a partir da base protegida em PRs futuros, exige que toda linha de amendment já `accepted` permaneça exatamente igual à linha da base confiável; verifica também a preservação das regressões aceitas 002/003/004, `fail_closed`, bindings de regressão 001–005 e o registro do conjunto efetivo de verificadores no Source of Truth Index. Uma nova revisão pode acrescentar nova linha/novo amendment, mas não pode reescrever título, documento, deviation record, evidence binding, validation history ou qualquer outro campo de uma linha aceita anterior.

## 12. Proteção externa obrigatória

`audit/REPOSITORY_PROTECTION_POLICY.json` exige na `main`: PR obrigatório; >=1 aprovação; dismissal de aprovação obsoleta; required checks; enforcement para administradores; force push/deletion desabilitados. Bootstrap exige `Evolution governance gate`; depois de CS016E, também `Trusted governance root gate`. Sem confirmação pela API, CS016E não está aceito e não libera CS017.

## 13. Release derivado e atestado

`release_authorized=true` sozinho é insuficiente. Release exige todos os stages accepted, bindings exatos commit/evidence/decision, evolution verifier, release assurance verifier, final acceptance verifier e artifact attestation criptograficamente verificável. `verify_release_attestation.py` usa `gh attestation verify`. Se attestations para repo privado não estiverem disponíveis no plano/configuração, release permanece `BLOCKED`; JSON local não é substituto.

## 14. SCN-REGRESSION-005

Deve provar permanentemente: empty paths em writes => REJECT; `allowed_paths=["**"]` => REJECT; forbidden obrigatório removido => REJECT; candidato não escolhe base; scope não muda após start; root/juiz/workflow/authorizer steady-state não pode ser modificado por amendment comum; linhas aceitas do amendment ledger e acceptance chain não podem ser reescritas/truncadas; regressões aceitas 002/003/004 não podem desaparecer ou perder evidência; provenance local sem correspondência GitHub não basta; release incompleto => REJECT; stage futuro sem máximo => REJECT.

## 15. Requisitos e invariantes

`EVREQ-075..EVREQ-086` em `audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016E.json`; `INV-EV-031..INV-EV-041` em `audit/EVOLUTION_INVARIANTS_AMENDMENT_016E.json`.

## 16. Limites

CS016E não altera runtime, ABI, comportamento canônico, testes de produto, claims comerciais ou release da v1.14.1. Não promete impossibilidade matemática de toda vulnerabilidade futura; transforma brechas conhecidas em regressões fail-closed e separa autoridade confiável do candidato para reduzir falso PASS silencioso.
