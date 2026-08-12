# NeoEng D-Core — Política Simples de Validação de ChangeSets

Documento ID: `NEOENG-DCORE-CHANGESET-VALIDATION-001`  
Estado deste documento nesta branch: **TRANSITION CANDIDATE / NÃO ATIVO NA MAIN**

## 1. Objetivo

Esta política recupera o fluxo operacional que funcionou nos ChangeSets CS001–CS015 — ChangeSet com objetivo claro, base conhecida, implementação, testes específicos, evidência e fechamento — acrescentando somente controles necessários para impedir falso PASS, mascaramento de falhas e aceitação baseada apenas em uma bolinha verde do CI.

Ela não reescreve nem invalida evidências históricas. CS001–CS016 permanecem registros históricos. A transição é prospectiva.

## 2. Estados

O fluxo normal possui apenas quatro estados:

`PLANNED -> IMPLEMENTED -> VALIDATED -> ACCEPTED`

`FAILED` e `BLOCKED` são estados terminais de não aceitação para uma tentativa/campanha. Uma tentativa falha pode ser sucedida por uma nova tentativa corrigida, mas a falha anterior continua preservada.

## 3. Plano de validação vem antes da campanha

Antes da execução qualificante deve existir `VALIDATION_PLAN.json` já commitado. Ele fixa:

- ChangeSet e objetivo;
- `base_sha` exato;
- workflow de execução;
- lista completa dos testes obrigatórios;
- job/step que constitui a evidência de cada teste;
- arquivos de teste, workflow e verificadores que ficam congelados para aquela campanha.

A lista não pode ser reduzida depois que a campanha começou. Um teste obrigatório não pode ser removido, pulado ou convertido em opcional para obter PASS.

Se um teste ou workflow realmente precisar mudar, a campanha corrente é encerrada sem aceitação, a falha/limitação é preservada e um novo plano/campanha é criado. Os testes afetados são executados novamente.

## 4. Regra de validação

Um ChangeSet somente pode chegar a `VALIDATED` quando **todos** os testes declarados como obrigatórios retornarem `PASS` e a evidência oficial corresponder ao plano.

Para teste obrigatório, qualquer um dos estados abaixo bloqueia validação e aceitação:

- `FAIL`;
- `SKIPPED`;
- `NOT_TESTED`;
- `BLOCKED`;
- `PARTIAL`;
- resultado ausente.

Não existe equivalência entre `CI success` e `VALIDATED`.

## 5. Binding exato da execução

Cada resultado qualificante deve registrar, no mínimo:

- `source_sha` completo de 40 caracteres;
- `run_id` oficial do GitHub Actions;
- `run_attempt` oficial;
- `workflow_path`;
- hash SHA-256 do plano congelado;
- resultado individual de cada teste obrigatório.

O verificador consulta a API oficial do GitHub e exige correspondência exata de repositório, SHA, `run_id`, `run_attempt`, workflow, conclusão e jobs/steps declarados.

## 6. Congelamento anti-mascaramento

O `plan_commit` é a âncora da campanha. O verificador compara os bytes atuais do plano e de cada `frozen_file` contra os bytes existentes naquele commit.

Se workflow, teste ou verificador congelado for alterado depois do `plan_commit`, a campanha deixa de ser qualificante. Não é permitido alterar o juiz depois de ver o resultado e usar a mesma campanha como prova.

## 7. Aceitação

`ACCEPTED` exige que o resultado já seja validado e não contenha nenhum erro bloqueante. O fechamento é um registro posterior à campanha; o commit de implementação e o commit de fechamento podem ser diferentes, desde que a ancestralidade Git seja provada.

A aceitação de um ChangeSet **não autoriza automaticamente release**. Release continua sendo uma decisão separada, com seus próprios requisitos.

## 8. Falhas são evidência

Toda falha relevante deve permanecer identificável por SHA/run/attempt e motivo. Corrigir a causa raiz e obter um novo PASS é permitido; apagar, reclassificar ou esconder a falha anterior não é.

## 9. Modelo single-maintainer

Este repositório opera com um único mantenedor humano. A política não inventa uma segunda pessoa, não exige conta alternativa e não chama automação de “revisão humana independente”.

O controle técnico para PRs futuros é um verificador proveniente da `main` protegida. Em `pull_request_target`, o script e a política vêm da base confiável e o candidato é tratado somente como dados. O código candidato não é executado pelo gate confiável.

## 10. Formato mínimo de um ChangeSet futuro

Cada ChangeSet deve conter, no mínimo:

- `CHANGESET.md` — objetivo, escopo e limites;
- `VALIDATION_PLAN.json` — plano congelado antes da campanha;
- `VALIDATION_RESULT.json` — binding SHA/run/attempt e resultados;
- evidências adicionais quando necessárias;
- registro explícito de falhas anteriores da mesma campanha/ChangeSet.

O descriptor `audit/CURRENT_CHANGESET_VALIDATION.json` aponta para o plano e, depois da campanha, para o resultado corrente.

## 11. O que esta política deliberadamente não recria

Ela não exige uma cadeia de amendments para cada ajuste operacional, `ACTION_SCOPE` autorreferente, uma árvore extensa de verificadores de verificadores ou “CI verde = aceito”. Controles históricos podem continuar arquivados como evidência, mas o fluxo prospectivo deve permanecer pequeno, legível e verificável.

## 12. Transição a partir do CS016E

A atual root CS016E foi criada com intenção de endurecimento e produziu controles úteis, mas também ficou auto-restritiva: sua política steady-state bloqueia substituição da própria root sem autoridade externa, e o verificador corrente determina trabalho ativo a partir de amendment `in_progress`, criando um paradoxo de fechamento quando a própria linha passa a `accepted` e ainda há diff de closure.

Por isso esta branch não afirma estar “aceita pelo root antigo”. A substituição é apresentada como **transição explícita de regime**. Enquanto a `main` continuar exigindo os checks antigos, um REJECT deles contra esta proposta é esperado e deve ser preservado, não contornado.

Nenhuma proteção da `main` deve ser alterada e nenhum merge deve ocorrer sem autorização explícita para efetivar a troca de regime.
