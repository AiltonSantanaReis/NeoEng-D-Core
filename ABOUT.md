# About NeoEng D-Core

O **NeoEng D-Core** é uma infraestrutura determinística C++23 para autoridade canônica de estado, transições reproduzíveis, rollback, replay, recuperação e evidência verificável.

Ele foi projetado como produto horizontal: aplicações, hosts, interfaces, renderers, telemetria e adapters consomem o núcleo por fronteiras oficiais, mas não recebem autoridade direta para modificar o estado canônico.

## Posicionamento técnico

O D-Core concentra cinco responsabilidades principais:

- autoridade canônica de estado;
- transições determinísticas dentro de contratos versionados;
- linha temporal com checkpoints, replay, rollback e ressimulação;
- evidência de estado por hashes, SHA-256, Merkle, traces e support bundles;
- integração controlada por Host SDK e superfícies oficiais.

O projeto também contém uma referência distribuída limitada, mecanismos de recuperação/observabilidade e o View Lab somente leitura. Cada uma dessas superfícies tem limites próprios; sua existência não autoriza extrapolações para consenso, transporte remoto de produção, certificação, ARM64 ou desempenho universal.

## Estado de referência

A release histórica aceita é `v1.14.1`, no commit `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

Esta modernização parte da baseline administrativa pós-release `d092ac56290d76dddf51982549a98234f038f3ee`, cujo commit declara explicitamente não alterar produto/runtime/ABI.

A evolução pós-1.14.1 é governada pelo programa `POST_1_14_1`. O roadmap aponta `EV-00` como estágio corrente, ainda `not_started`, com `CS017` planejado e sem autorização de nova release.

## D-Core não é D-Lab

O D-Core é o **produto sob teste e evolução**. Infraestrutura D-Lab é um **laboratório externo de validação**. Evidência externa pode apoiar decisões governadas, mas não passa a integrar o runtime nem amplia claims do produto automaticamente.

## Autoridade documental

A fonte primária é:

- `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`;
- `audit/SOURCE_OF_TRUTH_INDEX.json`;
- ledgers e contratos apontados por esse índice.

README, guias e este arquivo são materiais explicativos e não substituem a autoridade normativa.

## Descrição sugerida para o GitHub About

> Deterministic C++23 canonical-state infrastructure with rollback, replay, verifiable state evidence, recovery, and an installable C Host SDK.

## Topics sugeridos

`cpp`, `cpp23`, `deterministic-systems`, `state-management`, `rollback`, `replay`, `cmake`, `simulation`, `verification`, `reproducibility`, `observability`, `software-assurance`

A visibilidade do repositório é uma configuração administrativa e não altera o significado de “public API”, “public header” ou “public package”, que aqui designam superfícies externas suportadas do produto.
