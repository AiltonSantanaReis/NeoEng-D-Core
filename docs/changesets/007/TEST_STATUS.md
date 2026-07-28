# ChangeSet 007 — status de testes

Base verificada: NeoEng D-Core 1.6.0
Versão resultante: NeoEng D-Core 1.7.0
Ambiente executado: Linux x86_64 virtualizado

## Suítes completas

| Compilador | Configuração | Resultado | Tempo real CTest |
|---|---|---:|---:|
| GCC 14.2.0 | Release; Full Toolset, Research Tools, View Lab, Host SDK e instalação habilitados | 58/58 | 11,79 s |
| Clang 17.0.0 | Release; Full Toolset, Research Tools, View Lab, Host SDK e instalação habilitados | 58/58 | 6,86 s |

As quatro verificações classificadas como Host SDK passaram em ambos os compiladores:

- testes funcionais da ABI;
- compilação e execução do header por compilador C;
- host de referência escrito em C;
- instalação em prefixo limpo seguida de `find_package()`, build e execução de consumidor externo.

## Sanitizers

Clang 17.0.0 com AddressSanitizer e UndefinedBehaviorSanitizer executou 6/6 testes críticos:

- `neoeng_operational_hardening_tests`;
- `neoeng_session_recovery_contract_tests`;
- `neoeng_state_evidence_tests`;
- `neoeng_host_sdk_tests`;
- `neoeng_host_sdk_c_header_test`;
- `neoeng_host_sdk_reference`.

Nenhuma falha de ASan ou UBSan foi reportada.

## Equivalência entre compiladores

As saídas foram byte a byte idênticas entre GCC e Clang:

| Probe | SHA-256 da saída |
|---|---|
| Host SDK reference | `8ad2bd6c8922dde353fb24f386455f82c8f4c6f0afa95c8cd866527f6fa99538` |
| Determinism probe | `d787bb9afc9f55a3c7f79803f5cad46bdb181484060214f250109531926a4ade` |
| State-evidence probe | `fe2e91e747400295a40e47884c2a8cc7acf13cdbb759dd5e5f46748f4d89263e` |

Os 14 símbolos públicos da ABI C foram extraídos das bibliotecas estáticas GCC e Clang e os conjuntos foram idênticos.

O hash canônico de regressão permaneceu `0x0FBBFF1EDC7B9346`. Os vetores SHA-256 e Merkle do estado permaneceram inalterados.

## Invariantes e isolamento

- 64 headers canônicos em `include/neoeng/core` permaneceram byte a byte idênticos à base 1.6.0;
- 58 fontes canônicas em `src` permaneceram byte a byte idênticas à base 1.6.0;
- a lista CMake das 58 fontes do núcleo permaneceu inalterada;
- `neoeng_dcore` não depende de `modules/host_sdk`;
- o Host SDK depende unidirecionalmente de `neoeng_dcore`;
- o View Lab continua opcional e somente leitura;
- o verificador de isolamento do projeto passou;
- `git diff --check` passou.

## Limitações não convertidas em resultados

- nenhum teste foi executado em Windows físico;
- nenhum teste foi executado em ARM64;
- os layouts ABI foram definidos e verificados por `static_assert` no ambiente x86_64, mas a confirmação nativa ARM64 permanece diferida;
- o wrapper PowerShell de instalação foi criado e revisado, mas não executado neste ambiente;
- não há distribuição de biblioteca compartilhada em 1.7.0; o Host SDK é estático;
- não foram implementados adapters Unreal, Unity, ROS 2 ou verticais;
- nenhum perfil P0–P4 foi qualificado;
- nenhuma certificação ou compatibilidade binária entre toolchains C++ foi reivindicada. A fronteira estável destinada a hosts é a ABI C 1.0.

Os logs executados estão em `docs/changesets/007/evidence/`.
