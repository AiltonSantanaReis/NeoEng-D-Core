# NeoEng D-Core 1.7.0 — Verificação independente da evidência Windows
## Decisão
O pacote bruto de evidências foi recebido e verificado. Os sete manifestos oficiais cobrem **351 arquivos**, e todos os 351 arquivos existem, possuem o tamanho declarado e reproduzem o SHA-256 registrado. As **1.308 verificações automatizadas** estão registradas como aprovadas, e as **37 execuções** usam somente códigos de saída previstos pelo contrato de cada campanha.
A validação Windows pertence ao commit `fb8362602e1b3f6530d3efe8733bc76fc6de9f3e`, árvore Git `eb5f1ac2f772cc34937062343d3ec7ce21c4868e`. `git fsck --full` passou. A árvore extraída aparece modificada no Linux somente pela conversão LF/CRLF; `git diff --ignore-space-at-eol` não encontra alteração de conteúdo contra o commit.
## Limite da conclusão
Esta verificação recalcula integridade, hashes, resultados registrados, objetos Git e equivalência da baseline. Ela não reexecuta binários PE do Windows no Linux. O perfil P1 continua **UNQUALIFIED**, conforme a própria campanha fail-closed.
## Campanhas
| Campanha | Arquivos no manifesto | Verificados | Checks | Checks aprovados | Execuções | Saídas inesperadas |
|---|---:|---:|---:|---:|---:|---:|
| run-01-20260723-173228 | 41 | 41 | 73 | 73 | 11 | 0 |
| run-02a-20260723-180959 | 39 | 39 | 233 | 233 | 6 | 0 |
| run-02b-20260723-183232 | 40 | 40 | 332 | 332 | 7 | 0 |
| run-03-20260723-184637 | 23 | 23 | 187 | 187 | 3 | 0 |
| run-04-20260723-191421 | 22 | 22 | 200 | 200 | 3 | 0 |
| run-05-20260723-200027 | 19 | 19 | 57 | 57 | 2 | 0 |
| run-06-20260723-204317 | 167 | 167 | 226 | 226 | 5 | 0 |
| **Total** | **351** | **351** | **1308** | **1308** | **37** | **0** |

## Evidência de binários e CTest

As 30 execuções que registram `ExecutableSHA256` foram cruzadas com os binários presentes no pacote; **30/30 hashes correspondem**.

Existe um log completo preservado com **58/58 testes aprovados e zero falhas**. O arquivo `validation-summary.txt` afirma que a suíte completa foi executada duas vezes, porém o pacote não preserva dois logs completos distintos. Portanto, uma execução completa é verificável diretamente; a segunda permanece uma afirmação do resumo, não uma prova separadamente reproduzível.

## Divergência histórica identificada
A fonte realmente testada no Windows não é byte a byte igual à árvore anteriormente reconstruída apenas pelos pacotes CS001–CS007. Dos 280 arquivos rastreados no commit, 275 coincidem integralmente ou após normalização de fim de linha; cinco possuem conteúdo adicional:
| Arquivo | Natureza da diferença |
|---|---|
| `.gitignore` | repository hygiene only |
| `cmake/NeoEngDCoreConfig.cmake.in` | Windows clang-cl compiler-rt package dependency |
| `modules/view_lab/CMakeLists.txt` | Windows clang-cl builtins linkage for View Lab |
| `scripts/windows/build.ps1` | force C and C++ frontends to clang-cl |
| `tests/cmake/run_host_sdk_install_consumer.cmake` | Windows vcpkg prefix and consumer executable resolution |

Nenhum desses cinco arquivos pertence ao conjunto canônico `src/` ou `include/neoeng/core/`. Portanto, não foi encontrada divergência na semântica do núcleo determinístico; a divergência é de build, empacotamento e integração Windows. Ainda assim, ela precisava ser formalmente corrigida porque os artefatos originalmente entregues não reproduziam exatamente a fonte testada.
## Baseline recuperada
Foi criada uma baseline 1.7.0 autoritativa usando a árvore integral reconstruída e substituindo exatamente os cinco arquivos pela versão do commit testado. O manifesto contém 1.187 entradas e foi recalculado integralmente.
- ZIP autoritativo: SHA-256 `4cba06b214c9f224a216ebb86b29f371b557bb3ee21f6d404d9f048fa602b2c0`
- Manifesto interno: SHA-256 `702bdf68f1db68658f6db34c17db096a8cc13f2864d850b74d11137c9bd66991`
- Patch de reconciliação: SHA-256 `77485f1898bb08180937419368249247e3ee766301548cae4bdff48d15ee6bf9`
- Fonte limpa do commit testado: SHA-256 `a6d25003ad2fb60da9b979e9b9c5a7d8fe4e0661c8dcd983c2a897437332b104`

## Consequência normativa
O commit testado e a baseline recuperada substituem a reconstrução anterior como ponto de partida do CS008. O CS008 deve declarar explicitamente essa reconciliação; não deve afirmar que o pacote CS007 original já continha os cinco ajustes.
