# Relatório de validação executável — NeoEng D-Core

Data: 22 de julho de 2026.

## Identidade e integração

- projeto CMake: `NeoEngDCore`;
- biblioteca: `neoeng_dcore`;
- alias público: `NeoEng::DCore`;
- pacote vcpkg: `neoeng-d-core`;
- runner Windows: `scripts/windows/run-dcore.ps1`;
- busca de identidade anterior na árvore ativa: zero ocorrências.

## Integridade e completude

- 353/353 arquivos obrigatórios permanecem cobertos: 351 byte a byte idênticos à origem e 2 modificações autorizadas/hashadas em `audit/AUTHORIZED_SOURCE_MODIFICATIONS.json`;
- 59 headers, 53 fontes de biblioteca, 72 fontes de aplicação e 3 fontes de teste após o ChangeSet 002;
- 166 registros documentais e 23 scripts de campanha preservados com SHA-256 da origem;
- 72/72 fontes de aplicação e 53/53 fontes de núcleo aparecem no grafo CMake;
- nenhuma dependência ativa de renderização ou unidades do Ano 2;
- 433 arquivos de evidência original do Ano 1 preservados.

## Build e testes Linux x86_64

### GCC 14.2.0

- configuração e build Release: aprovados;
- 72 executáveis no diretório raiz do build;
- CTest: 39/39 aprovados, 0 falhas;
- tempo real da suíte: 11,80 s.

### Clang 17.0.0

- configuração e build Release: aprovados;
- 72 executáveis no diretório raiz do build;
- CTest: 39/39 aprovados, 0 falhas;
- tempo real da suíte: 11,33 s.

### Comparação determinística

O `neoeng_determinism_probe 10000` produziu saída idêntica em GCC e Clang, com hash canônico `0x0FBBFF1EDC7B9346`. O `neoeng_dcore_lab` também produziu saída idêntica, com hash `0x4D94BB2F3D75FFFE`.

Os builds foram retomados algumas vezes porque o executor encerrou invocações longas por limite temporal. O Ninja completou ambos os builds e não houve falha final de compilação ou linkedição.

## Windows

Windows x64 com clang-cl, Ninja, Windows SDK e vcpkg permanece o perfil operacional primário. O pacote NeoEng D-Core não foi executado em Windows neste host Linux. Binários Windows encontrados na origem continuam preservados apenas como proveniência.

## Gates físicos ainda não fechados

ARM64 real, Linux bare-metal, soak físico de 72 horas e medição de rollback em hardware Windows físico continuam pendentes. A auditoria de renomeação não converte ausência de campanha física em aprovação técnica.

## ChangeSet 001 — segurança, observabilidade, recuperação e hardware

A versão 1.1.0 adiciona uma suíte separada de hardening e executáveis de fuzz/fault injection. Os resultados detalhados estão em `docs/changesets/001/TEST_STATUS.md`.

Critérios avaliados:

- vetor HMAC-SHA256 RFC 4231;
- autenticação, alteração de payload, timestamp, sessão, anti-replay e rate limiting;
- parser de comandos sem alocação e rejeição por tamanho/domínio;
- fuzz de datagramas hostis;
- trace limitado, correlação e overwrite explícito;
- captura, navegação, diff e exportação time-travel;
- fluxo end-to-end do `OperationalRuntime`;
- fault injection da máquina de recuperação;
- P1 aprovado apenas com baseline completo, ambiente coincidente e métricas dentro de 2,0 ms/0,1 ms;
- ausência de baseline classificada como `UNQUALIFIED`.
## Resultado final do ChangeSet 001

No estado final do patch, o build incremental integral e a suíte completa foram repetidos após os últimos reforços de validação:

- GCC 14.2.0: 39/39 testes, 0 falhas, 11,80 s;
- Clang 17.0.0: 39/39 testes, 0 falhas, 11,33 s;
- 72 executáveis em cada diretório raiz de build;
- fuzz externo: 100.000 datagramas hostis por compilador, sem exceção;
- ASan+UBSan: suíte operacional reduzida aprovada;
- saídas de fuzz, fault injection, time-travel, determinismo, D-Core Lab e gate `unqualified` idênticas entre GCC e Clang.

Nenhuma execução Windows ou qualificação física P0-P3 foi adicionada por esta validação.


## ChangeSet 002 — sessão e recuperação formal

No estado final da versão 1.2.0:

- GCC 14.2.0 Release: build aprovado; 43/43 testes, 0 falhas, 14,38 s;
- Clang 17.0.0 Release: build aprovado; 43/43 testes, 0 falhas, 14,24 s;
- 76 executáveis no diretório raiz de cada build;
- 100.000 hellos hostis adicionais por compilador: 0 aceitos e 100.000 rejeitados;
- ASan+UBSan: suítes operacional e sessão/recuperação aprovadas sem diagnóstico;
- probes de sessão, recuperação, determinismo e D-Core Lab byte a byte idênticos entre compiladores;
- hashes determinísticos anteriores preservados.

A primeira compilação Clang detectou um shift de largura inválida com warnings como erro. O acumulador little-endian foi corrigido para `uint64_t` e toda a regressão foi repetida.

A validação não inclui Windows físico, transporte UDP/QUIC, HSM/TPM, auditoria criptográfica independente, adapters Unreal/Unity ou fault injection físico.

## ChangeSet 003 validation

The optional View Lab was built and tested with GCC 14.2.0 and Clang 17.0.0 on the virtualized Linux x86_64 audit host. Both full suites passed 46/46 tests. A reduced Clang ASan/UBSan build passed all three View Lab tests.

A deterministic CLI campaign generated 31 visual frames plus HTML and correlation metadata. GCC and Clang produced the same 33 output files and identical SHA-256 values. Core determinism and D-Core Lab canonical hashes remained unchanged.

The verification script now distinguishes the canonical core boundary from the optional companion module. It rejects reverse dependencies and validates imported Year-2 source/evidence hashes against the extraction ledger.

## ChangeSet 004 — validação de evidência criptográfica

A versão 1.4.0 foi compilada com warnings como erro em GCC 14.2.0 e Clang 17.0.0. As duas suítes completas passaram 49/49 testes. O fuzz externo executou, por compilador, 100.000 provas Merkle válidas e 100.000 adulterações de um bit; todas as válidas foram aceitas e todas as adulteradas foram rejeitadas.

O probe produziu saídas byte a byte idênticas entre compiladores. O SHA-256 canônico de regressão foi `4f0423c219fcc6f43aa6aaeaa38617114fc8b73abb291b1b4e120c2a981e1730`, e a Merkle Root foi `1d0684929f913733a318d9349890730a53b8a3642fc5c895c0779acd9f98b342`.

Clang ASan+UBSan aprovou as suítes de hardening operacional, sessão/recuperação e evidência de estado. O determinism probe permaneceu em `0x0FBBFF1EDC7B9346`, e o D-Core Lab em `0x4D94BB2F3D75FFFE`.

A campanha não inclui Windows físico, ARM64, provedor assimétrico, armazenamento append-only, notário externo ou auditoria criptográfica independente.

## ChangeSet 005 — observabilidade e suporte

A versão 1.5.0 foi validada em Linux x86_64 virtualizado com GCC 14.2.0 e Clang 17.0.0. Ambas as suítes passaram 52/52. O subconjunto crítico Clang ASan+UBSan passou 4/4. Os probes de support bundle, fuzz, determinismo e D-Core Lab foram idênticos entre compiladores. A campanha de adulteração rejeitou 100.000/100.000 bundles por compilador. Windows físico, ARM64 e perfis P0-P4 continuam como gates diferidos.

## ChangeSet 006 — harness de qualificação

A versão 1.6.0 foi compilada com warnings como erro em GCC 14.2.0 e Clang 17.0.0 no ambiente Linux x86_64 virtualizado. As duas suítes completas passaram 54/54 testes, em 6,82 s e 6,85 s respectivamente. Os diretórios raiz de build contiveram 84 executáveis cada, 89 quando incluídos executáveis em subdiretórios.

O escopo novo foi exercitado com Clang ASan+UBSan:

- `neoeng_hardware_qualification_tests`: aprovado;
- workload ECS com 64 amostras: concluído sem diagnóstico de sanitizer, hash final `0x92E8E1ACEF6E3D6F`.

O runner e o verificador independente aceitaram campanhas P0 completas de GCC e Clang, mas o contrato as classificou corretamente como `unqualified` / `engineering_baseline`, porque o host é virtualizado. Uma cópia adulterada foi rejeitada por divergência de tamanho/hash.

Os probes de determinismo e evidência de estado foram byte a byte idênticos entre GCC e Clang, com SHA-256 `d787bb9afc9f55a3c7f79803f5cad46bdb181484060214f250109531926a4ade` e `fe2e91e747400295a40e47884c2a8cc7acf13cdbb759dd5e5f46748f4d89263e`. Tempos de relógio de parede não foram comparados entre compiladores.

A campanha não executou Windows físico, ARM64, NVIDIA, AMD ou P4. Nenhum perfil P0–P4 foi qualificado. O escopo completo Y1-O2 para P1 permanece incompleto até que evidências aceitas de alocação geral, arena e copy-on-write sejam vinculadas à campanha, além do benchmark de manutenção de índices já automatizado.


## ChangeSet 007 — Host SDK e empacotamento

A versão 1.7.0 foi compilada com warnings como erro em GCC 14.2.0 e Clang 17.0.0 no ambiente Linux x86_64 virtualizado. Em ambos, a suíte completa com Full Toolset, Research Tools, View Lab, Host SDK e instalação habilitados passou 58/58 testes. O teste de instalação criou um prefixo limpo, configurou um consumidor externo por `find_package()`, compilou e executou o binário.

As saídas do host de referência, do determinism probe e do state-evidence probe foram byte a byte idênticas entre GCC e Clang. Um subconjunto de seis testes críticos foi executado com Clang ASan+UBSan sem falhas. Os 64 headers e 58 fontes canônicos do núcleo foram comparados com a base 1.6.0 e permaneceram byte a byte idênticos.

Windows físico, ARM64, shared-library ABI e adapters de engine não foram executados ou reivindicados. Evidências: `docs/changesets/007/evidence/`.
