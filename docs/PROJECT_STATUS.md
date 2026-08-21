# NeoEng D-Core — Estado do projeto

Este documento é uma **visão executiva e não normativa** do estado atual do NeoEng D-Core. Em caso de divergência, prevalecem a fonte de verdade e os ledgers indicados em [`../audit/SOURCE_OF_TRUTH_INDEX.json`](../audit/SOURCE_OF_TRUTH_INDEX.json).

## Identidades principais

| Item | Identidade / estado |
|---|---|
| Produto | NeoEng D-Core |
| Release histórica aceita | `v1.14.1` |
| Commit da release | `e3fff973554a2e56b8bd7afdc1132f75f3ec337c` |
| Base Git usada por esta modernização documental | `d092ac56290d76dddf51982549a98234f038f3ee` |
| Tree dessa base | `d8e89bad4cce8d2a8c592aa1d2a7b568987005d8` |
| Host SDK | ABI C `1.0` |
| Programa evolutivo | `POST_1_14_1` |
| Estado do programa | `active` |
| Estágio corrente no roadmap | `EV-00` |
| Estado de `EV-00` | `not_started` |
| ChangeSet planejado | `CS017` |
| Nova release autorizada | `false` |

## Release aceita versus desenvolvimento pós-release

A release `v1.14.1` é uma identidade histórica protegida. O repositório continuou recebendo manutenção de governança depois da publicação.

O commit `d092ac56290d76dddf51982549a98234f038f3ee`, usado como base desta modernização, registra a aposentadoria dos triggers automáticos de workflows legados CS016 e declara explicitamente **nenhuma mudança de produto/runtime/ABI**.

Consequentemente:

- `v1.14.1` continua sendo a release aceita;
- uma árvore pós-release não deve ser renomeada informalmente como nova release;
- manutenção de governança posterior não amplia claims da release;
- evidência de uma identidade não deve ser transferida para outra sem binding explícito.

## Produto horizontal

O D-Core permanece um produto independente de infraestrutura determinística. Seu escopo não inclui, por si só:

- renderer/editor;
- engine de IA;
- áudio;
- implementação setorial;
- PKI/HSM;
- transporte WAN de produção;
- consenso/BFT/quorum;
- certificação externa.

Essas exclusões são fronteiras de produto, não defeitos ocultos.

## Estado de capacidades

A baseline 1.14.1 contém superfícies verificadas e implementadas para estado canônico, determinismo no corpus declarado, replay/rollback, state evidence, Host SDK, recovery/observability, support bundles, distributed reference limitada e View Lab.

O escopo público exato é controlado por [`commercial/PUBLIC_CLAIMS.md`](commercial/PUBLIC_CLAIMS.md) e [`../audit/PRODUCT_CLAIMS_LEDGER.json`](../audit/PRODUCT_CLAIMS_LEDGER.json).

## Evolução pós-1.14.1

O programa `POST_1_14_1` está ativo e preserva a release histórica. O roadmap atual aponta `EV-00` — certificação da baseline no laboratório — como estágio corrente, mas seu estado continua `not_started`.

O ChangeSet planejado para esse estágio é `CS017`. O próprio estado de governança registra que uma nova release não está autorizada.

```text
baseline v1.14.1 aceita
        ↓
governança pós-release preparada
        ↓
EV-00 é o próximo estágio
        ↓
EV-00 ainda não iniciou
        ↓
nenhuma nova release autorizada
```

## D-Core versus D-Lab

A governança do D-Core contém requisitos para um D-Lab externo porque EV-00 depende de validação independente do produto. Isso **não funde os projetos**.

- D-Core: produto, runtime, contratos, claims e roadmap.
- D-Lab: infraestrutura externa de execução/verificação.
- Evidência externa: só passa a sustentar uma decisão do D-Core quando o fluxo de provenance/ChangeSet aplicável a incorporar.
- Arquivos e governança de um repositório não são automaticamente autoridade no outro.

## Assurance e limitações

A release 1.14.1 possui evidência registrada para ambientes declarados, incluindo lanes Linux GCC/Clang, Windows clang-cl e controles de release assurance associados aos ChangeSets históricos.

Ainda assim, a documentação não deve inferir automaticamente:

- ARM64 qualificado;
- hardware P0–P4 universalmente qualificado;
- long-run/power-loss físico;
- auditoria externa;
- certificação;
- desempenho universal;
- adequação mission-critical irrestrita.

Use [`RESULTS_AND_CLAIMS_GUIDE.md`](RESULTS_AND_CLAIMS_GUIDE.md) para interpretar resultados.

## Documentos de autoridade

A ordem real de precedência é definida em [`../audit/SOURCE_OF_TRUTH_INDEX.json`](../audit/SOURCE_OF_TRUTH_INDEX.json). Os pontos de entrada mais importantes são:

- [`governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`](governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md)
- [`../audit/SOURCE_OF_TRUTH_INDEX.json`](../audit/SOURCE_OF_TRUTH_INDEX.json)
- [`governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md`](governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md)
- [`governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md`](governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md)
- [`../audit/EVOLUTION_ROADMAP.json`](../audit/EVOLUTION_ROADMAP.json)
- [`commercial/PUBLIC_CLAIMS.md`](commercial/PUBLIC_CLAIMS.md)

## Estado desta modernização documental

Esta página foi preparada como documentação explicativa. Ela não:

- inicia EV-00;
- inicia CS017;
- altera roadmap;
- autoriza release;
- muda ABI;
- muda runtime;
- muda claims;
- importa resultado de laboratório;
- reclassifica evidência histórica.
