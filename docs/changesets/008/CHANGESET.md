# ChangeSet 008 — contrato normativo, rastreabilidade e baseline de asseguração

**Base obrigatória:** NeoEng D-Core 1.7.0 autoritativa
**SHA-256 da base:** `4cba06b214c9f224a216ebb86b29f371b557bb3ee21f6d404d9f048fa602b2c0`
**Commit Windows testado da base:** `fb8362602e1b3f6530d3efe8733bc76fc6de9f3e`
**Versão resultante:** NeoEng D-Core 1.8.0

## Objetivo fechado

Transformar a definição de produto, as promessas, as responsabilidades, as limitações e os critérios de conclusão em uma fonte normativa única e verificável. O ChangeSet impede que capacidades sejam declaradas concluídas por implementação parcial, demonstração estreita ou ausência de evidência.

O CS008 não fecha os requisitos técnicos ainda abertos. Ele cria a baseline obrigatória para fechá-los integralmente nos ChangeSets seguintes.

## Entregas normativas

- fonte normativa de verdade e índice de precedência;
- padrão de conclusão de capacidades e de ChangeSets;
- padrão de testes e asseguração por requisito;
- matriz de 36 requisitos do produto;
- ledger de 20 claims públicos;
- matriz de responsabilidade e fronteiras do produto;
- backlog finito de 41 limitações conhecidas;
- matriz de asseguração e 10 campanhas obrigatórias;
- plano fechado de conclusão CS009–CS015;
- reconciliação explícita dos documentos técnicos, comerciais e de licenciamento;
- ledger de validações nativas e externas diferidas.

## Controles executáveis

`verify_product_contract.py` valida de forma fail-closed:

- schemas e versão dos ledgers;
- contagens exatas e IDs únicos;
- estados permitidos;
- referências de implementação e evidência;
- precedência da fonte normativa;
- classificação de responsabilidades;
- coerência dos claims e limitações;
- proibição de pré-fechar o escopo técnico reservado ao CS009.

Seu autoteste altera sete classes de dados e exige rejeição em todos os casos.

`verify_product_assurance.py` valida:

- cobertura dos 36 requisitos na matriz de asseguração;
- classes mínimas de teste por responsabilidade e risco;
- ligação dos requisitos a 10 campanhas;
- artefatos obrigatórios e verificador independente;
- proibição de inferir execução nativa ou externa;
- distinção entre meta-verificação e comprovação técnica.

Seu autoteste altera nove classes de dados e exige rejeição em todos os casos.

Os quatro testes foram integrados ao CTest com labels `governance` e `release-gate`.

## Preservação do núcleo

O ledger `audit/CHANGESET_008_CORE_INVARIANT_LEDGER.json` comparou 122 arquivos canônicos:

- 58 fontes em `src/`;
- 64 headers em `include/neoeng/core/`.

Resultado: **122 inalterados; 0 divergências**.

Não foram modificados estado canônico, transição, fixed tick, rollback, serialização, stable hash, SHA-256, Merkle ou autoridade do D-Core.

## Alterações de versão e integração

A versão do pacote passou para 1.8.0. A ABI C permanece 1.0; somente a versão de runtime/reporting foi atualizada para refletir o pacote 1.8.0. Os testes do Host SDK foram atualizados para essa identidade, sem mudança de layout ou semântica da ABI.

## Ocorrência de desenvolvimento preservada

Na primeira execução GCC, `neoeng_host_sdk_tests` falhou porque o teste ainda esperava runtime minor 7. A implementação já reportava minor 8. O teste foi corrigido, a suíte completa foi recompilada e executada novamente em GCC e Clang. O log da primeira falha está preservado como `gcc-ctest-initial-failure.log`; somente os logs finais são evidência qualificadora deste ChangeSet.

## Não objetivos

O CS008 não implementa nem afirma concluir:

- evidência ECS completa ou qualificação P1;
- equivalência ARM64;
- duas instâncias distribuídas e reconciliação;
- fechamento numérico;
- durabilidade temporal completa;
- assinatura assimétrica ou confidencialidade de produção;
- release signing, SBOM ou certificação;
- Unreal, Unity, ROS 2 ou verticais setoriais.

## Estado de saída

A baseline 1.8.0 é uma baseline de governança e asseguração. O produto permanece **não concluído comercialmente**, com 21 requisitos internos e 25 limitações internas abertas. Cada item deverá ser fechado de ponta a ponta conforme as campanhas normativas, sem promoção de claims acima da evidência.
