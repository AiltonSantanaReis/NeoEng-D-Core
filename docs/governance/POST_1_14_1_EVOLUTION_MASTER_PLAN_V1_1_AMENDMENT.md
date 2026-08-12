# NeoEng D-Core — Amendment Normativo 1.1 do Plano de Evolução Pós-1.14.1

Documento normativo: `NEOENG-DCORE-EVOLUTION-001`  
Versão normativa efetiva: **1.1**  
Base incorporada por referência: `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md` versão 1.0  
ChangeSet autorizador: `CS016A`  
Baseline histórica protegida: `v1.14.1`  
Commit histórico protegido: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## 1. Natureza do amendment

Este documento é um amendment append-only do Plano Mestre 1.0. A versão 1.0
permanece preservada no histórico Git e continua válida em tudo que não for
explicitamente alterado por este amendment. Em conflito explícito, este
documento controla por possuir precedência superior no
`audit/SOURCE_OF_TRUTH_INDEX.json`.

A combinação:

`Plano Mestre 1.0 + este amendment = Plano Mestre efetivo 1.1`.

Nenhuma disposição deste amendment altera retroativamente `v1.14.1`, CS001-CS015,
CS016, evidência histórica ou claims já publicados.

## 2. Motivo e achado de processo

A auditoria preparatória do laboratório histórico e da cópia local do produto
identificou duas classes de risco que o Plano 1.0 ainda não tornava
suficientemente bloqueantes:

1. a necessidade de revalidar, sem reescrever a história, decisões e evidências
   de CS001-CS015 sob um laboratório novo e reprodutível;
2. a necessidade de impedir mecanicamente que uma ação operacional seja
   executada antes da formalização e aceitação do amendment que a autoriza.

O segundo item foi confirmado por um incidente de processo: foi proposta a
execução de um preflight PRE-CS017 antes de CS016A existir e ser aceito. Nenhum
runtime, branch `main`, baseline ou evidência histórica foi alterado por esse
incidente, porém a tentativa demonstrou que controle de aceitação de etapa não
era suficiente para controlar autorização pré-execução.

O registro causal é `docs/records/evolution/DEV-0001.md`.

## 3. Regra de STOP até aceitação de CS016A

Enquanto `CS016A` não estiver `accepted`:

- EV-00 permanece `not_started`;
- CS017 não pode iniciar;
- nenhum PRE-CS017 pode ser executado;
- nenhuma construção do D-Lab v2 pode ser tratada como campanha qualificante;
- nenhum build de baseline para EV-00 pode ser iniciado;
- nenhuma evidência produzida para EV-00 pode ser usada para aprovação;
- somente ações necessárias para construir, verificar, evidenciar e aceitar
  CS016A são autorizadas.

A autorização é verificável por `scripts/authorize_evolution_action.py`.

## 4. Quarta camada operacional: autorização fail-closed

O modelo de três níveis do Plano 1.0 permanece válido:

1. documento normativo humano;
2. ledgers legíveis por máquina;
3. ChangeSets e evidências imutáveis.

Este amendment acrescenta uma **camada operacional de execução**, sem substituir
os três níveis:

4. **Action Authorization Gate fail-closed**.

Fluxo obrigatório:

`SOT -> plano efetivo -> ledgers -> ChangeSet -> action authorization -> ação -> evidência -> verificação -> decisão`.

Uma ação não autorizada não pode produzir patch, comando qualificante, campanha,
avanço de etapa ou release.

`REJECT` não é warning e não pode ser ignorado.

## 5. D-Lab v2

O laboratório histórico CS001-CS015 é preservado como material histórico e
fonte de comparação. Ele não é promovido diretamente a laboratório oficial do
EV-00.

O D-Lab v2 deve ser reconstruído operacionalmente em workspaces novos, mas pode
reutilizar técnicas históricas somente depois de auditoria e revalidação.

O contrato normativo do laboratório é:
`docs/governance/DLAB_VALIDATION_STANDARD.md`.

O laboratório:

- valida o D-Core somente pelas fronteiras oficiais aplicáveis;
- não fornece comportamento necessário ao runtime;
- não modifica o source-under-test;
- mantém source, build, install e evidência separados;
- não recicla build antigo como prova de run novo;
- identifica product SHA, harness SHA, configuração e ambiente;
- registra toda tentativa antes do primeiro teste;
- preserva falhas e resultados parciais;
- fecha cada run em estado terminal inequívoco;
- usa evidência bruta append-only;
- exige verificação independente do manifesto;
- distingue simulação, execução real e campanha física;
- nunca promove ambiente não executado.

## 6. Revalidação histórica CS001-CS015

EV-00 passa a incluir **Historical Assurance Revalidation**.

`audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json` deve conter exatamente CS001
até CS015, sem lacunas ou duplicatas.

A revalidação possui duas dimensões independentes:

1. **integridade histórica** — conferir identidade, hashes, manifests, logs e
   demais evidências existentes;
2. **reprodutibilidade atual** — reconstruir e reexecutar o que for tecnicamente
   reproduzível.

Revalidar não significa reescrever a história.

Se uma conclusão histórica for contradita ou considerada excessiva, a evidência
original permanece intacta e um novo finding append-only registra:

- origem histórica;
- conclusão original;
- nova evidência;
- impacto;
- disposição atual;
- necessidade ou não de correção futura.

Estados não conclusivos ou ambiente perdido permanecem explicitamente não
qualificados.

Itens classificados como `critical` ou `high` e ainda reproduzíveis devem ser
reexecutados antes da aceitação de EV-00.

A classificação inicial pode permanecer `unclassified` até CS017 realizar a
auditoria de cada ChangeSet; nenhuma classificação pode ser inventada.

## 7. Corpus operacional de cenários

O D-Lab v2 deve manter `audit/DLAB_SCENARIO_CATALOG.json`.

Classes mínimas obrigatórias:

- `normal`;
- `integration`;
- `degraded`;
- `adversarial`;
- `recovery`;
- `soak`;
- `combinatorial`;
- `regression`.

Todo cenário qualificante deve possuir:

- ID e versão;
- classe;
- tipo (`real`, `simulated`, `hybrid` ou `physical`);
- risco;
- precondições;
- passos;
- oracle, propriedade ou classe de rejeição esperada;
- escopo de claim;
- ambiente aplicável;
- seed e versão do gerador quando houver randomização.

Execução simulada não prova comportamento físico. Teste de interrupção lógica
não promove claim de power-loss físico. Teste local/loopback não promove
transporte remoto de produção.

## 8. Regra de regressão permanente

Todo defeito confirmado no produto, laboratório, verificador ou governança deve
gerar um teste ou cenário permanente capaz de reproduzir a condição relevante,
quando tecnicamente possível.

O incidente de processo que motivou este amendment gera obrigatoriamente o
cenário `SCN-REGRESSION-001`: tentativa de `PRE-CS017` enquanto CS016A não está
aceito deve ser rejeitada.

## 9. Patch e alteração local

Toda alteração executada no computador do proprietário deve possuir:

- HEAD esperado;
- branch esperada;
- working tree previamente classificada;
- hashes/precondições dos arquivos atingidos quando aplicável;
- allowlist de caminhos;
- proibição de fallback fuzzy;
- `git apply --check --whitespace=error-all` quando o mecanismo for patch;
- aplicação integral ou nenhuma aplicação;
- `git diff --check`;
- confirmação pós-aplicação dos caminhos alterados.

`git apply --reject`, aplicação parcial intencional, `git add -A` indiscriminado,
`git reset --hard`, `git clean -fdx` e force-push não são mecanismos padrão de
recuperação desta governança.

Qualquer uso excepcional exige ação explicitamente autorizada, justificativa e
evidência.

## 10. Isolamento local obrigatório

A topologia recomendada e governada para EV-00 é:

```text
C:\NeoEng\
├── Archive\
│   ├── D-Lab-historical\
│   └── D-Core-pre-EV00\
├── Control\
│   └── NeoEng-D-Core\
└── Validation\
    └── CS017-<run-id>\
        ├── harness\
        ├── source\
        ├── build\
        ├── install\
        └── evidence\
            ├── raw\
            ├── derived\
            └── verification\
```

Nomes físicos podem variar desde que as separações e identidades sejam
equivalentes e registradas.

Archive é não destrutivo. Control governa patches. Validation contém workspaces
descartáveis/reconstruíveis. Nenhuma dessas áreas pode ser confundida com
evidência histórica aceita.

## 11. Requisitos adicionais

Os requisitos adicionais do EV-00 são normativos em
`audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json`.

O conjunto adicional é `EVREQ-055..EVREQ-071`.

Eles complementam EVREQ-001..EVREQ-004; não os substituem.

Nenhum requisito adicional pode ser considerado `verified` sem evidência
registrada.

## 12. Invariantes adicionais

Os invariantes adicionais são normativos em
`audit/EVOLUTION_INVARIANTS_AMENDMENT_016A.json`.

O conjunto adicional é `INV-EV-021..INV-EV-027`.

Eles complementam INV-EV-001..INV-EV-020 e permanecem ativos durante todo o
programa pós-1.14.1.

## 13. Estado e evidência de amendments

`audit/EVOLUTION_AMENDMENTS.json` é o ledger de amendments.

Um amendment pode usar:

- `in_progress`;
- `blocked`;
- `failed`;
- `accepted`;
- `superseded`.

Somente `accepted` satisfaz uma precondição `required_before_stage`.

Aceitação exige:

- source commit exato;
- manifesto de evidência;
- `TEST_STATUS.md` aceito;
- todos os verificadores aplicáveis;
- GitHub Actions sobre o SHA candidato;
- validação final no estado `accepted`;
- validação pós-merge em `main`.

## 14. Action Authorization Gate

`scripts/authorize_evolution_action.py` é o autorizador operacional.

Classes mínimas:

- `governance_amendment`;
- `prepare_stage_changeset`;
- `start_stage`;
- `stage_operation`;
- `advance_stage`;
- `release`.

Regras mínimas:

- `governance_amendment` só pode atuar sobre amendment conhecido e
  `in_progress`;
- qualquer ação ligada a EV-00 é rejeitada enquanto existir amendment
  `required_before_stage=EV-00` não aceito;
- `prepare_stage_changeset` exige stage atual `not_started`, ChangeSet planejado
  correspondente e amendments prévios aceitos;
- `stage_operation` exige stage `in_progress`;
- `advance_stage` exige dependências e etapa anterior aceitas;
- `release` exige `release_authorized=true` e closure aplicável;
- caminhos fornecidos devem respeitar `ACTION_SCOPE.json` do ChangeSet;
- ausência de informação necessária resulta em `REJECT`.

## 15. Gate de entrada do EV-00 revisado

Depois que CS016A estiver `accepted`, CS017 poderá ser preparado.

EV-00 só pode iniciar quando:

1. CS016 está `accepted`;
2. CS016A está `accepted`;
3. `verify_evolution_plan.py` aceita;
4. `verify_dlab_governance.py` aceita;
5. `authorize_evolution_action.py` autoriza `prepare_stage_changeset`;
6. CS017 possui ChangeSet e ACTION_SCOPE;
7. baseline histórica continua `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
8. nenhuma melhoria funcional foi aplicada.

## 16. Gate de saída do EV-00 revisado

Além dos critérios originais do Plano 1.0, EV-00 só pode ser `accepted` quando:

- D-Lab v2 passou seus self-tests negativos;
- baseline v1.14.1 foi reconstruída em workspace novo;
- build e CTest suportados possuem evidência;
- determinism probe, Host SDK, replay/rollback, state evidence e support bundle
  possuem evidência aplicável;
- CS001-CS015 foram avaliados na matriz histórica;
- reexecuções obrigatórias `critical/high` reproduzíveis foram concluídas;
- findings históricos foram preservados append-only;
- corpus obrigatório de cenários foi executado no escopo aplicável;
- cenários randomizados registraram seed;
- todas as tentativas possuem estado terminal;
- manifests verificam;
- gerador e verificador de evidência não dependem de confiança cega no mesmo
  resultado;
- ambientes não executados permanecem não qualificados;
- nenhum teste/gate foi enfraquecido;
- os requisitos EVREQ-001..004 e requisitos adicionais de CS016A aplicáveis a
  EV-00 estão `verified`.

## 17. Não objetivos de CS016A

CS016A não autoriza:

- alteração em `src/` ou `include/`;
- alteração de ABI;
- mudança de replay, rollback, snapshot ou serialização;
- alteração de comportamento canônico;
- mudança de claim público;
- release;
- início de EV-00;
- execução de PRE-CS017;
- reclassificação retroativa de evidência histórica.

## 18. Critério de aceitação deste amendment

CS016A somente pode ser `accepted` quando:

- este documento e o D-Lab Standard estão registrados na Source of Truth;
- ledgers adicionais verificam;
- `verify_dlab_governance.py --self-test` passa;
- `verify_dlab_governance.py` passa;
- `authorize_evolution_action.py --self-test` passa;
- o autorizador rejeita especificamente PRE-CS017 no estado `in_progress`;
- `verify_evolution_plan.py --self-test` continua passando;
- `verify_evolution_plan.py` continua passando;
- verificadores de produto e assurance continuam passando;
- `MANIFEST.sha256` confere;
- evidência de CI é preservada;
- o estado `accepted` é validado novamente;
- após merge, a `main` passa novamente pelos gates.

Até esse fechamento, CS017 e EV-00 permanecem bloqueados.
