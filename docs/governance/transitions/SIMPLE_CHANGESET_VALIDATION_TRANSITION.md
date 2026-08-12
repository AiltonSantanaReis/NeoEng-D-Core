# Controlled transition candidate — Simple ChangeSet Validation

Status: **AUTHORIZED FOR CONTROLLED ACTIVATION / NOT YET MERGED / POST-MERGE REVALIDATION REQUIRED**

Base exata da proposta: `main@d20a40d0cd0952d653965b34b67ec30e9ba1b42f`.

## Motivo

O objetivo é substituir prospectivamente a camada operacional pós-CS016 por um processo equivalente ao usado com sucesso em CS001–CS015, preservando as melhorias realmente úteis: SHA exato, `run_id`, `run_attempt`, plano de testes congelado antes da execução, evidência oficial, falhas preservadas e separação explícita entre `VALIDATED` e `ACCEPTED`.

## Segurança da transição

Esta proposta não tenta fingir conformidade com o root que pretende substituir. A root atual declara seus próprios arquivos steady-state imutáveis e bloqueia substituição sem autoridade externa independente. No modelo single-maintainer declarado, essa autoridade não existe.

Consequentemente:

1. a proposta foi publicada em branch separada;
2. a `main` permaneceu intacta durante a preparação;
3. os checks antigos rejeitaram a proposta e esses resultados são preservados;
4. nenhum resultado antigo é reclassificado como PASS;
5. a troca de required checks/merge é tratada explicitamente como mudança de regime, não como uma “aprovação” produzida pelo sistema antigo.

Em 2026-08-12 o mantenedor declarou autorização explícita para ativar a mudança de regime. A autorização permite a execução do bootstrap administrativo descrito abaixo, mas não transforma a PR em aceita antes da revalidação pós-merge.

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

## Bootstrap administrativo autorizado

O novo `Trusted ChangeSet validation gate` é um job de `pull_request_target`. Por definição ele só se torna um juiz confiável quando `.github/workflows/changeset-validation.yml`, `scripts/verify_changeset_validation.py` e a policy já existem na base protegida `main`. Portanto, ele não pode ser o required check que autoriza o próprio primeiro merge de bootstrap.

A ativação deve usar uma janela mínima e auditável:

1. manter PR obrigatório, enforcement administrativo, bloqueio de force-push e deletion;
2. remover temporariamente apenas os required checks antigos que tornam a troca de regime impossível;
3. mesclar exclusivamente a PR #28 no head exato validado do candidato;
4. observar o `Main ChangeSet validation` no commit de merge;
5. imediatamente configurar `Trusted ChangeSet validation gate` como required check estrito da `main`;
6. provar o novo gate em uma PR subsequente sob o novo regime;
7. somente após esses passos declarar a transição estabilizada.

Durante a janela, a ausência temporária de required checks é um desvio administrativo registrado por `DEV-0006`, não um estado de aceitação. Se a proteção não for restaurada ou a validação pós-merge falhar, o estado é **BLOCKED/FAILED**, não aprovado.

## Limite desta PR

Esta PR instala apenas o candidato de política/verificador/workflow e templates. Ela não inicia CS017, não executa EV-00, não altera runtime/produto, não fecha retroativamente CS016E e não autoriza release.
