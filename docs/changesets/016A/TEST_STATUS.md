# ChangeSet 016A — status de validação

State: in_progress

## Estado atual

A primeira campanha candidata pré-aceitação passou no source commit
`eaa37e84d6e5a32830b01177ee0531b260ea97b5` (run `31591410348`). Ao promover o
ledger experimentalmente para `accepted`, o run `31591689618` expôs um defeito
real no self-test do Action Authorization Gate: o teste herdava o estado atual
do amendment e, portanto, deixou de ser válido depois da promoção.

A promoção foi revertida para `in_progress`. Nenhuma evidência de falha foi
apagada e CS017/EV-00 continuam bloqueados.

## Gates executados

| Gate | Estado | Evidência |
|---|---|---|
| D-Lab action authorization self-test no candidato pré-aceitação | PASS | run `31591410348` |
| D-Lab governance self-test no candidato pré-aceitação | PASS | run `31591410348` |
| D-Lab governance verifier no candidato pré-aceitação | PASS | run `31591410348` |
| Anti-skip PRE-CS017 no candidato pré-aceitação | PASS | run `31591410348` |
| Evolution verifier self-test | PASS | run `31591410348` |
| Evolution verifier | PASS | run `31591410348` |
| Product contract verifier | PASS | run `31591410348` |
| Product assurance verifier | PASS | run `31591410348` |
| Manifest | PASS | run `31591410348` |
| Accepted-state Action Authorization self-test | FAILED | run `31591689618` |

## Causa da falha aceita como achado

O self-test usava o ledger corrente para testar o cenário `in_progress`.
Quando o próprio ledger passou a `accepted`, isso gerou três falhas corretas no
runner: trabalho de amendment já aceito foi rejeitado, PRE-CS017 passou a ser
autorizável e o bloqueador CS016A deixou de existir. O teste confundiu estado
real com fixture de regressão.

A correção obrigatória é tornar o self-test independente do estado corrente,
criando explicitamente fixtures `in_progress` e `accepted` e verificando ambos.

## Regra de continuidade

CS016A só poderá voltar a `accepted` depois que a correção do self-test produzir
novo source commit, novo run candidato integralmente PASS e nova evidência
vinculada ao SHA corrigido.

`NOT_TESTED`, `BLOCKED`, `FAILED` ou evidência de SHA anterior nunca equivalem à
aprovação do código corrigido.
