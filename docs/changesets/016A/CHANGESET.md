# ChangeSet 016A — D-Lab v2 governance amendment

ID: `CS016A`  
Tipo: amendment normativo de governança  
Baseline de entrada: `main` `938f47168af7f645872a64821a5324fb9af281f6`  
Baseline histórica protegida: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Stage afetado: EV-00, ainda `not_started`

## 1. Objetivo

Formalizar antes de CS017:

1. D-Lab v2 como laboratório fail-closed;
2. revalidação histórica CS001-CS015;
3. corpus de cenários reais/simulados/adversariais;
4. proveniência do harness e evidência append-only;
5. Action Authorization Gate contra avanço fora da sequência;
6. regressão permanente do incidente PRE-CS017 antecipado.

## 2. Causa

O Plano Mestre 1.0 exigia evidência rigorosa, porém não possuía regra
operacional suficientemente mecânica para impedir a proposta/execução de uma
ação do estágio seguinte antes da formalização de um amendment recém-identificado.

O incidente está em `docs/records/evolution/DEV-0001.md`.

## 3. Escopo autorizado

Somente arquivos listados em `docs/changesets/016A/ACTION_SCOPE.json`.

O ChangeSet pode:

- adicionar amendment normativo;
- adicionar D-Lab Standard;
- adicionar ledgers suplementares;
- registrar requirements/invariants suplementares;
- adicionar verificador do D-Lab;
- adicionar Action Authorization Gate;
- atualizar Source of Truth Index;
- atualizar workflow de governança;
- atualizar `MANIFEST.sha256`;
- registrar evidência de CS016A.

## 4. Não objetivos

Este ChangeSet não altera:

- `src/`;
- `include/`;
- testes do runtime/produto;
- ABI;
- Host SDK;
- replay;
- rollback;
- snapshots;
- serialização;
- semântica canônica;
- claims públicas;
- release;
- baseline v1.14.1.

Não inicia EV-00 e não autoriza PRE-CS017.

## 5. Invariantes

Preserva INV-EV-001..INV-EV-020 e adiciona INV-EV-021..INV-EV-027 por ledger
suplementar.

Nenhum invariante existente é enfraquecido.

## 6. Requisitos

Preserva EVREQ-001..EVREQ-054 e adiciona EVREQ-055..EVREQ-071 para EV-00.

Os requisitos novos permanecem `planned` durante CS016A; CS016A apenas os torna
normativos. Verificação operacional ocorrerá em CS017/EV-00 conforme aplicável.

## 7. Gate anti-skip obrigatório

Enquanto CS016A estiver `in_progress`, a solicitação:

- action: `prepare_stage_changeset`
- stage: `EV-00`
- changeset: `CS017`

deve retornar `REJECT`.

A ação `governance_amendment` para `CS016A` deve permanecer autorizável dentro
da allowlist.

## 8. Validações obrigatórias

Antes de aceitar CS016A:

- `python3 scripts/verify_dlab_governance.py --self-test`;
- `python3 scripts/verify_dlab_governance.py`;
- `python3 scripts/authorize_evolution_action.py --self-test`;
- `python3 scripts/authorize_evolution_action.py --action governance_amendment --changeset CS016A`;
- prova negativa de PRE-CS017;
- `python3 scripts/verify_evolution_plan.py --self-test`;
- `python3 scripts/verify_evolution_plan.py`;
- `python3 scripts/verify_product_contract.py`;
- `python3 scripts/verify_product_assurance.py`;
- `python3 scripts/generate_manifest.py --check`;
- GitHub Actions no SHA candidato;
- evidence manifest;
- nova validação no estado accepted;
- validação pós-merge em `main`.

## 9. Critério de falha

Qualquer:

- path fora da allowlist;
- runtime file alterado;
- verificador ausente;
- ledger inconsistente;
- self-test negativo aceito;
- PRE-CS017 autorizado prematuramente;
- manifesto divergente;
- CI não executado/failed;
- evidência ausente;

mantém CS016A não aceito e CS017 bloqueado.

## 10. Decisão inicial

`State: in_progress`.

Nenhuma evidência não executada é tratada como PASS.
