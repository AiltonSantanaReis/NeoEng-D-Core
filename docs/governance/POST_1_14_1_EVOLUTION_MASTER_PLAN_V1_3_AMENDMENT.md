# NeoEng D-Core — Plano Mestre Pós-v1.14.1 — Amendment 1.3

Documento ID: `NEOENG-DCORE-EVOLUTION-001-A3`  
Programa: `POST_1_14_1`  
Baseline histórica protegida: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Versão normativa efetiva: **1.3**

## 1. Natureza append-only

Este documento complementa, sem reescrever ou relaxar:

1. `POST_1_14_1_EVOLUTION_MASTER_PLAN.md` — versão 1.0;
2. `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md` — versão 1.1;
3. `POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md` — versão 1.2;
4. `DLAB_VALIDATION_STANDARD.md` — versão 1.0.

O fluxo que originou este amendment permanece:

`STOP -> deviation record -> impact analysis -> amendment -> verification -> resume`

Nenhum teste, gate, allowlist, oracle, requisito, invariante, claim ou critério anterior é removido ou enfraquecido.

## 2. Desvio que originou CS016C

Desvio: `DEV-0003`.

Após CS016B ser aceito e incorporado à `main`, CS017 foi reiniciado a partir da nova `main`.
O estado inicial reconciliado de CS017 chegou ao commit de controle
`871f4c571f776e599c136ccbd131123003a69a77`, com EV-00 `in_progress` e `start_stage` autorizado.

Antes da publicação ou execução do harness qualificante, a análise do próximo
`stage_operation` revelou que o caminho real previamente allowlisted
`.github/workflows/ev00-dlab.yml` não podia ser reconhecido pelo autorizador.

A função vigente normalizava caminhos com:

`path.replace("\\", "/").lstrip("./")`

Em Python, `str.lstrip("./")` remove qualquer sequência inicial formada pelos
caracteres `.` ou `/`; ele não remove somente o prefixo literal `./`.
Consequentemente, `.github/workflows/ev00-dlab.yml` era transformado em
`github/workflows/ev00-dlab.yml`, destruindo um componente de path válido e
fazendo a autorização divergir da própria allowlist.

Nenhum build da baseline, campanha D-Lab ou execução qualificante de produto foi
realizado depois da identificação desse bloqueio.

## 3. Causa raiz

Classificação: **governança / Action Authorization path canonicalization**.

A causa raiz é uso de uma primitiva de remoção por conjunto de caracteres onde
a semântica necessária era remoção de prefixo literal e validação canônica de
path relativo ao repositório.

O defeito não está no `ACTION_SCOPE` de CS017. O scope usa o caminho real e
correto `.github/workflows/ev00-dlab.yml`.

## 4. Regra de normalização preservadora

O Action Authorization Gate deve tratar paths concretos como caminhos relativos
canônicos do repositório.

A normalização autorizada deve:

1. converter `\\` para `/` somente como separador equivalente;
2. remover somente prefixos literais `./` completos;
3. preservar componentes cujo nome começa por ponto, inclusive `.github`;
4. rejeitar path vazio;
5. rejeitar path absoluto POSIX, UNC ou com drive Windows;
6. rejeitar componentes vazios, `.` ou `..` após a normalização permitida;
7. aplicar forbidden patterns antes de allowed patterns;
8. permanecer fail-closed quando a representação for ambígua ou inválida.

Não é permitido contornar a regra passando ao autorizador um nome diferente do
path real gravado no repositório.

## 5. Regressão permanente

Novo cenário obrigatório: `SCN-REGRESSION-003`.

Oracle mínimo:

- `.github/workflows/ev00-dlab.yml` explicitamente allowlisted => `AUTHORIZED`;
- `./.github/workflows/ev00-dlab.yml` => mesma identidade canônica e `AUTHORIZED`;
- `.github\\workflows\\ev00-dlab.yml` => mesma identidade canônica e `AUTHORIZED`;
- `.github/workflows/evolution-governance.yml` quando forbidden => `REJECT`;
- `../...`, path absoluto e drive path => `REJECT`;
- componentes ambíguos como `//` ou `/./` internos => `REJECT`.

A regressão qualifica apenas o mecanismo de governança. Ela não qualifica o
runtime NeoEng D-Core.

## 6. Requisito suplementar

### EVREQ-073

O Action Authorization Gate deve preservar nomes de componentes iniciados por
ponto durante a canonicalização de paths, remover somente prefixos relativos
literais autorizados e rejeitar representações absolutas, traversais ou
ambíguas de forma fail-closed.

Ledger: `audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016C.json`.

## 7. Invariante suplementar

### INV-EV-029

Canonicalização de path para autorização não pode alterar a identidade de um
componente válido do repositório nem transformar path inválido em path autorizado.

Enforcement:

- `scripts/authorize_evolution_action.py --self-test`;
- `SCN-REGRESSION-003`;
- `scripts/verify_dlab_governance.py`;
- workflow de governança.

Ledger: `audit/EVOLUTION_INVARIANTS_AMENDMENT_016C.json`.

## 8. Preservação do gate 1.2

A evolução do verificador para 1.3 deve preservar a verificação anterior de
forma demonstrável.

O verificador 1.3 deve reexecutar o verificador D-Lab 1.2 e seu self-test no
snapshot oficial aceito `855ff4563b96c4e5b2a7acac6e200fdf7f8d20d1` e, na árvore corrente, provar que
os documentos normativos 1.0/1.1/1.2, o D-Lab Standard e os registros aceitos
anteriores não foram reescritos para acomodar CS016C.

Arquivos compartilhados que precisam receber dados de CS016C devem manter os
registros anteriores byte/semanticamente preservados e receber somente o delta
append-only explicitamente validado.

## 9. Relação com CS017

Enquanto CS016C estiver diferente de `accepted`:

- CS017 permanece interrompido;
- a branch `agent/cs017-ev00-baseline-certification-r2` permanece evidência da tentativa interrompida;
- nenhum resultado daquela branch qualifica EV-00;
- EV-00 oficial em `main` permanece `not_started`;
- nenhum harness D-Lab qualificante pode ser executado.

Após CS016C ser aceito e incorporado à `main`, CS017 deve ser reconstruído a
partir da nova `main`, reler todas as regras aplicáveis e solicitar novamente
`prepare_stage_changeset`, `start_stage` e `stage_operation` conforme o lifecycle.

## 10. Impact analysis

### Runtime / ABI / dados canônicos

Sem impacto autorizado. CS016C não pode alterar `src/`, `include/`, CMake do
produto, testes do produto, Host SDK, replay, rollback, snapshots, serialização,
hashing canônico, formato persistente ou transições.

### Claims

Nenhuma expansão ou promoção.

### Release

Nenhuma release é autorizada. `release_authorized` permanece `false`.

### Histórico

`v1.14.1`, CS016, CS016A, CS016B e suas evidências permanecem imutáveis.

### Testes

Nenhum teste é removido, encurtado, ignorado ou relaxado. CS016C acrescenta
cobertura de path canonicalization e exige que as regressões anteriores continuem
passando.

## 11. ChangeSet corretivo

ChangeSet: `CS016C`.

Escopo exclusivo de governança:

- este Amendment 1.3;
- `DEV-0003`;
- ledgers suplementares de CS016C;
- registro append-only no ledger de amendments;
- `SCN-REGRESSION-003` e seu policy binding;
- correção conservadora de path canonicalization no Action Authorization Gate;
- evolução do D-Lab verifier preservando a prova 1.2;
- Source of Truth Index apenas para registrar os novos artefatos normativos;
- evidência e manifesto de CS016C.

Qualquer necessidade de tocar runtime ou regras anteriores é novo STOP e exige
novo desvio/amendment independente.
