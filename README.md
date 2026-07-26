# NeoEng D-Core

**Infraestrutura determinística de estado, simulação e rollback para aplicações host.**

O NeoEng D-Core é um núcleo C++23 standalone que atua como autoridade canônica sobre estado imutável, transições determinísticas, física numérica, checkpoints, replay e rollback. O projeto mantém renderização, GPU, UI, SDF, voxel e integrações verticais fora do núcleo, preservando uma arquitetura horizontal, auditável e reutilizável.

> **Estado atual:** baseline estável **1.9.0 / ChangeSet 009 concluído**. O **ChangeSet 010** é o ciclo ativo de desenvolvimento e trata de determinismo ponta a ponta entre duas instâncias independentes. A versão 1.10.0 ainda não é declarada concluída.

## Visão geral

O D-Core fornece uma fronteira controlada para hosts que precisam de:

- estado canônico e transições determinísticas;
- fixed tick, serialização canônica e hashes estáveis;
- checkpoints persistentes, replay e rollback;
- diagnóstico e localização de divergências;
- evidência criptográfica opcional baseada em SHA-256 e Merkle Tree;
- observabilidade, suporte operacional e recuperação formal;
- ABI C estável por meio do `NeoEng::DCoreHostSdk`;
- diagnóstico visual somente leitura por meio do módulo opcional `modules/view_lab`.

A dependência permanece unidirecional:

```text
host / adapter / módulo de diagnóstico
                 │
                 ▼
           NeoEng D-Core
        autoridade canônica
```

Consumidores externos recebem visões imutáveis ou cópias controladas. Renderer, telemetria, UI e adapters não modificam diretamente o estado canônico.

## Estado do produto

| Item | Situação |
|---|---|
| Baseline publicada | `1.9.0` |
| ChangeSet concluído mais recente | `CS009` — fechamento da evidência ECS Y1-O2 |
| Ciclo ativo | `CS010` — determinismo ponta a ponta e referência distribuída |
| Plataforma operacional inicial | Windows 10/11 x64 |
| Verificação adicional | Linux x86_64 com GCC e Clang |
| Qualificação P0–P4 | Não promovida automaticamente; depende de campanha específica |
| Certificação comercial | Não declarada |

A fonte normativa obrigatória está em [`docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`](docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md), apoiada pelos ledgers de máquina em [`audit/`](audit/). Em caso de conflito, esses artefatos prevalecem sobre documentos históricos.

## ChangeSet 010 — ciclo atual

O CS010 tem como objetivo demonstrar determinismo ponta a ponta entre duas instâncias independentes executando os mesmos inputs, com:

- comparação verificável de estado e resultados;
- injeção controlada de divergência;
- localização da primeira divergência relevante;
- reconciliação por contrato explícito;
- evidência reproduzível e verificável;
- transporte e coordenador de referência fora do estado canônico.

Consenso, quorum, BFT e banco distribuído permanecem fora do escopo do núcleo.

## Compilação rápida no Windows

### Pré-requisitos

- Windows 10 ou 11 x64;
- Developer PowerShell for Visual Studio 2022;
- Windows SDK;
- CMake 3.25 ou superior;
- Ninja;
- LLVM/clang-cl x64;
- Git;
- vcpkg no baseline fixado pelo projeto.

### Bootstrap, build e testes

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\run-dcore.ps1 -BootstrapDependencies
```

Para compilar toda a superfície e executar a suíte registrada:

```powershell
.\scripts\windows\run-dcore.ps1 -BootstrapDependencies -FullTestSuite
```

## Presets CMake

O repositório inclui presets oficiais para:

```text
windows-clang-release
windows-clang-debug
linux-gcc-release
```

Exemplo:

```powershell
cmake --preset windows-clang-release
cmake --build --preset windows-clang-release
ctest --preset windows-clang-release
```

## Verificação normativa

Os verificadores abaixo validam contrato, asseguração e integridade dos relatórios:

```powershell
python .\scripts\verify_product_contract.py --check-report
python .\scripts\verify_product_contract.py --self-test
python .\scripts\verify_product_assurance.py --check-report
python .\scripts\verify_product_assurance.py --self-test
```

Os autotestes são fail-closed e incluem cenários de adulteração. A aprovação dos verificadores demonstra coerência do pacote e das evidências declaradas, mas não substitui qualificação nativa, auditoria independente ou certificação quando essas etapas forem exigidas.

## Integração com hosts

O target principal é:

```cmake
NeoEng::DCore
```

A fronteira de integração instalável é fornecida por:

```cmake
NeoEng::DCoreHostSdk
```

Consumo por pacote CMake:

```cmake
find_package(NeoEngDCore 1.7 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE NeoEng::DCoreHostSdk)
```

A ABI C 1.0 utiliza handles opacos, códigos de status estáveis e estruturas com layout controlado. O Host SDK não expõe ponteiros internos e mantém uma política explícita de thread proprietária.

## Diagnóstico visual opcional

O módulo `modules/view_lab` é um consumidor somente leitura de snapshots e traces. Ele não participa da autoridade canônica e não introduz dependência de renderização no target `neoeng_dcore`.

No Windows:

```powershell
.\scripts\windows\run-view-lab.ps1 -BootstrapDependencies
```

O módulo produz artefatos determinísticos de diagnóstico, incluindo imagens BMP, viewer HTML e correlação de entidades e traces.

## Evidência e qualificação

A baseline 1.9.0 fecha o escopo de evidência ECS Y1-O2 para:

- alocação geral;
- arena;
- copy-on-write;
- manutenção de índices.

O verificador independente recalcula sequências, percentis, alocações, capacidade, overflow, reconstrução copy-on-write, hashes e vínculos de identidade de fonte, build e configuração.

Perfis P0–P4 são alvos de medição e comparação, não requisitos mínimos universais. Resultados descrevem apenas o ambiente registrado e não podem ser extrapolados para outras máquinas sem nova campanha.

A evidência Windows do CS009 está em [`docs/changesets/009/evidence/windows-host-20260725/REPORT.md`](docs/changesets/009/evidence/windows-host-20260725/REPORT.md). Como o ambiente reportou hipervisor ativo, ela é classificada como evidência host-local e não como qualificação `native_physical`.

## Estrutura do repositório

```text
apps/                    ferramentas, benchmarks, probes e fuzz targets
include/                 headers públicos do núcleo
src/                     implementação do NeoEng D-Core
modules/view_lab/        diagnóstico visual opcional e somente leitura
tests/                   testes C++
scripts/                 automação, verificação e campanhas
docs/                    arquitetura, contratos, governança e ChangeSets
audit/                   ledgers e relatórios de auditoria
cmake/                   suporte de build e exportação
```

## Documentação principal

- [Fonte normativa do produto](docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md)
- [Plano fechado de conclusão](docs/governance/PRODUCT_CLOSURE_PLAN.md)
- [Status de auditoria](docs/AUDIT_STATUS.md)
- [Relatório completo de auditoria](audit/NEOENG_DCORE_AUDIT_COMPLETE_REPORT.md)
- [Contrato da ABI C do Host SDK](docs/contracts/HOST_SDK_C_ABI_V1.md)
- [Fronteira do Host SDK](docs/architecture/HOST_SDK_BOUNDARY.md)
- [Fronteira do View Lab](docs/architecture/VIEW_LAB_BOUNDARY.md)

## Roadmap normativo

| ChangeSet | Escopo |
|---|---|
| CS010 | Determinismo ponta a ponta e referência distribuída |
| CS011 | Fechamento numérico |
| CS012 | Fechamento temporal e efeitos externos |
| CS013 | Segurança e evidência criptográfica de produção |
| CS014 | Release assurance e SDK completo |
| CS015 | Aceitação final |

Cada ChangeSet deve fechar integralmente o próprio escopo, preservar os invariantes do produto, atualizar os ledgers e apresentar evidência reproduzível antes de ser declarado concluído.

## Limites de declaração

O NeoEng D-Core ainda não é apresentado como produto comercialmente concluído, certificado ou universalmente qualificado. Claims de desempenho, segurança, compatibilidade e qualificação são limitados ao ambiente, corpus e evidência explicitamente registrados.

Consulte o [`PRODUCT_CLOSURE_PLAN.md`](docs/governance/PRODUCT_CLOSURE_PLAN.md) e o [`PRODUCT_CLAIMS_LEDGER.json`](audit/PRODUCT_CLAIMS_LEDGER.json) antes de publicar afirmações externas sobre o produto.
