# Controlled transition candidate — Simple ChangeSet Validation

Status: **PROPOSED / NOT ACCEPTED / NOT MERGE-AUTHORIZED**

Base exata da proposta: `main@d20a40d0cd0952d653965b34b67ec30e9ba1b42f`.

## Motivo

O objetivo é substituir prospectivamente a camada operacional pós-CS016 por um processo equivalente ao usado com sucesso em CS001–CS015, preservando as melhorias realmente úteis: SHA exato, `run_id`, `run_attempt`, plano de testes congelado antes da execução, evidência oficial, falhas preservadas e separação explícita entre `VALIDATED` e `ACCEPTED`.

## Segurança da transição

Esta proposta não tenta fingir conformidade com o root que pretende substituir. A root atual declara seus próprios arquivos steady-state imutáveis e bloqueia substituição sem autoridade externa independente. No modelo single-maintainer declarado, essa autoridade não existe.

Consequentemente:

1. a proposta é publicada em branch separada;
2. a `main` e suas proteções permanecem intactas;
3. os checks antigos podem rejeitar a proposta e esse resultado é evidência esperada;
4. nenhum check antigo será removido apenas para fazer a PR ficar verde;
5. uma troca de required checks/merge, se desejada, é uma decisão explícita de mudança de regime e não uma “aprovação” produzida pelo sistema antigo.

## Controles do regime proposto

- plano obrigatório e congelado antes da execução;
- lista de testes obrigatórios não pode diminuir após `plan_commit`;
- workflow/testes/verificadores declarados em `frozen_files` são byte-congelados;
- resultado obrigatório por teste;
- qualquer resultado obrigatório diferente de PASS bloqueia;
- binding obrigatório `source_sha + run_id + run_attempt + workflow_path`;
- API oficial GitHub confirma execução, attempt, SHA, workflow e jobs/steps;
- falhas anteriores permanecem registradas;
- CI verde isoladamente nunca é aceitação;
- trusted PR gate executa script/policy da base protegida, não a cópia candidata;
- nenhum reviewer humano fictício no modo single-maintainer.

## Limite desta PR

Esta PR instala apenas o candidato de política/verificador/workflow e templates. Ela não inicia CS017, não executa EV-00, não altera runtime/produto, não fecha CS016E e não autoriza release.
