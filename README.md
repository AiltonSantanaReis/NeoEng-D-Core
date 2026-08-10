# NeoEng D-Core

Infraestrutura C++23 para autoridade canônica de estado, transições determinísticas, replay, rollback e evidência verificável.

> **Release atual:** `v1.14.1` — publicado no tag [`v1.14.1`](https://github.com/AiltonSantanaReis/NeoEng-D-Core/releases/tag/v1.14.1), com CS014 no run `31387419484` e CS015 no run `31387421705`. O release público `v1.14.0` permanece histórico e imutável. A aceitação é restrita aos claims e limites registrados e não é certificação, auditoria externa, qualificação ARM64 ou garantia universal de desempenho.

## O que é

O NeoEng D-Core é um produto independente e horizontal. Ele mantém o estado canônico, aplica transições pela API oficial e fornece mecanismos para reconstrução, comparação e recuperação do estado. Hosts e módulos consumidores recebem snapshots, cópias controladas ou evidências; não alteram diretamente a autoridade canônica.

O núcleo não é um renderer, editor, motor de IA, sistema de áudio, biblioteca SDF/voxel ou integração vertical. Esses domínios podem existir como consumidores ou adaptadores futuros, sem fazer parte da autoridade do D-Core.

## Capacidades suportadas

- estado canônico com transições determinísticas e `fixed tick`;
- serialização canônica, hashes estáveis, SHA-256, Merkle e evidência encadeada;
- checkpoints, histórico, replay e correção/ressimulação dentro da janela contratada;
- localização de divergências por hash, SHA, Merkle e componente semântico;
- recuperação, observabilidade, traces e support bundles com integridade verificável;
- Host SDK instalável com ABI C estável 1.0 e handles opacos;
- referência distribuída limitada a duas instâncias independentes em UDP loopback;
- View Lab opcional, somente leitura, para inspeção de snapshots e traces.

## Arquitetura e fronteiras

```text
host / adapter / ferramenta companheira
                  │
                  ▼
       Host SDK ou integração oficial
                  │
                  ▼
            NeoEng D-Core
       autoridade canônica de estado
                  │
                  ▼
       snapshots, replay e evidências
```

A dependência é unidirecional: hosts, UI, telemetria, renderers e adapters consomem o núcleo, mas não recebem autoridade para mutar o estado canônico. O módulo distribuído é uma referência de integração; ele não fornece consenso, quorum, BFT, multiwriter, transporte remoto de produção ou `exactly once` sem um host conforme.

## Estado de validação

| Item | Estado documentado |
|---|---|
| Baseline do produto | release `v1.14.1` publicado e aceito (tag no merge `e3fff973`) |
| ChangeSets | CS001–CS015 preservados no histórico; CS015 histórico em `1.14.0` e aceito na candidata `1.14.1` |
| Requisitos internos obrigatórios | Nenhum aberto no ledger atual |
| Assurance de release | GCC/Clang Linux, clang-cl Windows, sanitizers, fuzzing, análise estática, SDK em prefixo limpo e evidência independente, conforme CS014 |
| CS014 do release | Windows clang-cl, Linux GCC/Clang, sanitizers, fuzzing, clang-tidy e atestação: aprovado no run [`31387419484`](https://github.com/AiltonSantanaReis/NeoEng-D-Core/actions/runs/31387419484) |
| Verificação corretiva de proveniência | Run [`31246260738`](https://github.com/AiltonSantanaReis/NeoEng-D-Core/actions/runs/31246260738); não altera a baseline do produto |
| Gates deferidos, não bloqueantes para CS015 | Qualificação nativa P0–P4/ARM64, long-run/power-loss, assurance externa e infraestrutura de deployment; continuam fora da aceitação horizontal |
| Qualificação nativa, ARM64 e certificação | Não declaradas; dependem de campanhas e contratos específicos |

Os números acima são evidências dos ambientes registrados. Eles não representam uma regra para qualquer outro computador. CPU, GPU, drivers, firmware, modo de energia, temperatura, virtualização e processos concorrentes podem alterar resultados.

## Requisitos para construir

O projeto é testado com:

- CMake `3.25` ou superior;
- C++23;
- Ninja;
- Boost `1.80` ou superior em configuração CMake;
- LLVM/clang-cl no Windows;
- GCC ou Clang no Linux;
- Git e o baseline de dependências indicado pelo manifesto vcpkg.

Os presets oficiais estão em [`CMakePresets.json`](CMakePresets.json):

```text
windows-clang-release
windows-clang-debug
linux-gcc-release
```

## Build e testes

### Windows — caminho recomendado

Abra um Developer PowerShell do Visual Studio e execute:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\run-dcore.ps1 -BootstrapDependencies -FullTestSuite
```

Para usar os comandos CMake diretamente:

```powershell
cmake --preset windows-clang-release
cmake --build --preset windows-clang-release --parallel 2
ctest --preset windows-clang-release --output-on-failure
```

O preset Windows pressupõe `VCPKG_ROOT` configurado e `clang-cl` disponível no ambiente do Developer PowerShell.

### Linux

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release --parallel
ctest --preset linux-gcc-release --output-on-failure
```

O resultado deve ser reportado junto com sistema operacional, compilador, versão do CMake, commit, configuração e saída bruta dos testes.

## Opções de build relevantes

As opções abaixo são controladas pelo CMake e não mudam a autoridade do núcleo:

| Opção | Uso |
|---|---|
| `NEOENG_DCORE_BUILD_HOST_SDK` | Gera o Host SDK instalável |
| `NEOENG_DCORE_BUILD_DISTRIBUTED_REFERENCE` | Gera a referência distribuída de duas instâncias |
| `NEOENG_DCORE_BUILD_VIEW_LAB` | Gera o diagnóstico visual somente leitura |
| `NEOENG_ENABLE_SANITIZERS` | Habilita sanitizers nas campanhas compatíveis |
| `NEOENG_ENABLE_LIBFUZZER` | Habilita os alvos de libFuzzer |
| `NEOENG_WARNINGS_AS_ERRORS` | Trata warnings como erros; habilitado por padrão |

## Integração com um host

Após instalar o pacote, o consumidor usa os targets exportados pelo CMake:

```cmake
find_package(NeoEngDCore CONFIG REQUIRED)

target_link_libraries(meu_host
    PRIVATE
        NeoEng::DCoreHostSdk
        # Opcional, quando a referência distribuída for necessária:
        # NeoEng::DCoreDistributedReference
)
```

O contrato C está em [`docs/contracts/HOST_SDK_C_ABI_V1.md`](docs/contracts/HOST_SDK_C_ABI_V1.md) e o header público está em [`modules/host_sdk/include/neoeng/dcore_host.h`](modules/host_sdk/include/neoeng/dcore_host.h). A ABI usa handles opacos, códigos de status estáveis, validação explícita e regras documentadas de thread e ownership; exceções C++ não atravessam a fronteira C.

## View Lab

O View Lab consome snapshots e traces sem alterar o estado canônico e sem introduzir dependência de renderização no núcleo:

```powershell
.\scripts\windows\run-view-lab.ps1 -BootstrapDependencies
```

Os artefatos gerados incluem diagnósticos determinísticos, imagens BMP e viewer HTML. Eles devem ser tratados como evidência de inspeção, não como uma nova autoridade de estado.

## Verificações normativas

Os verificadores fail-closed conferem os ledgers, contratos e relatórios declarados:

```powershell
python .\scripts\generate_manifest.py --check
python .\scripts\verify_product_contract.py
python .\scripts\verify_product_assurance.py
python .\scripts\verify_release_assurance.py
python .\scripts\verify_consolidated_release.py --self-test
```

Os autotestes dos verificadores também cobrem cenários de adulteração. Uma aprovação confirma a coerência do pacote e das evidências declaradas; não substitui qualificação física, auditoria independente, certificação ou avaliação contratual externa.

## Release, proveniência e evidência

Uma release deve ser analisada junto de seu manifesto, SBOM, proveniência, hashes e verificações independentes. O contrato de assurance está em [`docs/contracts/RELEASE_ASSURANCE_V1.md`](docs/contracts/RELEASE_ASSURANCE_V1.md). A baseline cumulativa aceita e seus limites estão registrados em [`docs/changesets/015/TEST_STATUS.md`](docs/changesets/015/TEST_STATUS.md), enquanto a campanha de release assurance está em [`docs/changesets/014/TEST_STATUS.md`](docs/changesets/014/TEST_STATUS.md).

O repositório não contém uma chave privada de assinatura. Quando uma distribuição exigir atestação externa, ela deve ser verificada pelo procedimento e pelos artefatos publicados para aquela release.

## Documentação normativa e de operação

- [Fonte de verdade do produto](docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md)
- [Índice de estado documental](docs/governance/DOCUMENT_STATUS_INDEX.md)
- [Plano de fechamento do produto](docs/governance/PRODUCT_CLOSURE_PLAN.md)
- [Padrão de conclusão do produto](docs/governance/PRODUCT_COMPLETION_STANDARD.md)
- [Padrão de testes de assurance](docs/governance/PRODUCT_ASSURANCE_TEST_STANDARD.md)
- [Claims públicas autorizadas](docs/commercial/PUBLIC_CLAIMS.md)
- [Contrato de qualificação de hardware](docs/contracts/HARDWARE_QUALIFICATION_V2.md)
- [Fronteira do Host SDK](docs/architecture/HOST_SDK_BOUNDARY.md)
- [Fronteira da referência distribuída](docs/architecture/DISTRIBUTED_REFERENCE_BOUNDARY.md)
- [Fronteira de segurança de produção](docs/architecture/PRODUCTION_SECURITY_BOUNDARY.md)
- [Fronteira do View Lab](docs/architecture/VIEW_LAB_BOUNDARY.md)
- [Ledger de gates deferidos](audit/DEFERRED_VALIDATION_GATES.json)
- [Relatório histórico de auditoria](docs/AUDIT_STATUS.md)
- [Avisos e atribuições](NOTICE.md)

## Estrutura do repositório

```text
include/                 headers públicos
src/                     implementação do núcleo
modules/                 Host SDK, referência distribuída e View Lab
tests/                   testes C++
scripts/                 build, verificação e campanhas
apps/                    ferramentas, probes e fuzz targets
cmake/                   exportação e suporte de build
docs/                    arquitetura, contratos e ChangeSets
audit/                   ledgers e relatórios de assurance
```

## Escopo e limites de declaração

As claims públicas devem ser lidas em conjunto com [`docs/commercial/PUBLIC_CLAIMS.md`](docs/commercial/PUBLIC_CLAIMS.md) e [`audit/PRODUCT_CLAIMS_LEDGER.json`](audit/PRODUCT_CLAIMS_LEDGER.json). Não se deve inferir a partir desta documentação:

- prontidão irrestrita para produção ou missão crítica;
- desempenho garantido por hardware, CPU, GPU ou sistema operacional;
- equivalência ARM64 ou qualificação nativa em qualquer máquina;
- consenso distribuído, BFT, quorum, multiwriter ou transporte remoto de produção;
- certificação, auditoria externa, PKI, custódia de chaves ou confiança em provider externo;
- prontidão setorial, ROI ou conformidade regulatória sem campanha específica.

Quando um novo ambiente, adapter, contrato ou requisito for necessário, ele deve ser tratado como uma campanha explicitamente definida, com corpus, configuração, evidência e decisão registrados. O README orienta o uso; a fonte normativa e os ledgers determinam o que pode ser declarado.
