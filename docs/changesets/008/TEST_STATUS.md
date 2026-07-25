# ChangeSet 008 — testes, evidências e limitações

## Ambiente executado

- Linux x86_64;
- GCC 14.2.0;
- Clang 17.0.0;
- CMake 3.31.6;
- Ninja 1.12.1;
- builds Release com warnings tratados como erro;
- Clang ASan + UBSan em subconjunto crítico, com LeakSanitizer desabilitado.

## Resultados finais

| Validação | Resultado |
|---|---:|
| GCC Release — configuração e build | aprovado |
| GCC CTest | 62/62, 0 falhas, 22,46 s |
| Clang Release — configuração e build | aprovado |
| Clang CTest | 62/62, 0 falhas, 16,44 s |
| Clang ASan + UBSan — subconjunto crítico | 14/14, 0 falhas, 9,63 s |
| Verificador da fonte de verdade | aprovado |
| Autoteste fail-closed do contrato | 7/7 adulterações rejeitadas |
| Verificador de asseguração | aprovado |
| Autoteste fail-closed de asseguração | 9/9 adulterações rejeitadas |
| Verificador de isolamento | aprovado |
| Verificador da fronteira Host SDK | aprovado |
| Invariantes canônicos | 122/122 inalterados |
| Determinism probe GCC vs Clang | idêntico |
| State-evidence probe GCC vs Clang | idêntico |
| Host SDK reference GCC vs Clang | idêntico |

## Identidades cross-compiler

- determinism probe: `d787bb9afc9f55a3c7f79803f5cad46bdb181484060214f250109531926a4ade`;
- state-evidence probe: `fe2e91e747400295a40e47884c2a8cc7acf13cdbb759dd5e5f46748f4d89263e`;
- Host SDK reference: `8ad2bd6c8922dde353fb24f386455f82c8f4c6f0afa95c8cd866527f6fa99538`.

## Evidências preservadas

A pasta `docs/changesets/008/evidence/` contém:

- configuração, build e CTest finais GCC/Clang;
- execução ASan + UBSan;
- outputs e diffs cross-compiler;
- logs dos verificadores normativos, de isolamento e Host SDK;
- ocorrência inicial de falha do teste de versão do Host SDK;
- verificação independente da campanha Windows 1.7.0.

## Interpretação correta

Os verificadores normativos comprovam coerência, cobertura e comportamento fail-closed dos ledgers. Eles **não comprovam automaticamente** os requisitos técnicos cuja campanha ainda está planejada.

Os resultados Linux 1.8.0 não qualificam Windows, ARM64 ou perfis P0–P4. A evidência Windows preservada corresponde à fonte 1.7.0 e não foi promovida para 1.8.0.

LeakSanitizer não foi executado. Não há claim de cobertura integral de leaks.

## Gates abertos

- regressão física Windows 1.8.0;
- execução física ARM64;
- campanhas P0–P4;
- fechamento integral ECS do CS009;
- determinismo ponta a ponta de duas instâncias do CS010;
- campanhas numéricas, temporais, criptográficas e de release dos CS011–CS014;
- auditoria final do CS015;
- auditorias e certificações externas quando aplicáveis.

## Decisão

O CS008 passa como ChangeSet de governança e asseguração. Ele não transforma requisitos técnicos planejados em capacidades comprovadas e não declara o produto comercialmente pronto.
