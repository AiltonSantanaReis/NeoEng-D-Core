# NeoEng D-Core - Fonte Normativa de Verdade do Produto

Documento normativo: `NEOENG-DCORE-SOT-001`
Versao normativa: 1.2
Produto governado: NeoEng D-Core
Baseline desta fonte: 1.14.0

## 1. Autoridade

Este documento e os registros de maquina listados em `audit/SOURCE_OF_TRUTH_INDEX.json` constituem a fonte normativa de verdade do NeoEng D-Core. Sua consulta e obrigatoria antes de definir ChangeSets, alterar escopo, declarar capacidade concluida, publicar claims, iniciar qualificacao ou aceitar adiamentos.

Nenhum plano historico, material comercial, conversa, auditoria anterior ou ChangeSet de menor precedencia prevalece quando houver conflito. O conflito deve interromper a afirmacao ou implementacao afetada ate reconciliacao auditavel.

## 2. Escopo definitivo

O NeoEng D-Core e um **produto independente** de infraestrutura deterministica. Ele consolida trabalho tecnico do Ano 1, mas **nao e a implementacao do programa completo de cinco anos** e nao deve ser administrado como base obrigatoria daquele programa separado.

Objetivo:

> Fornecer uma autoridade canonica de estado capaz de executar transicoes deterministicas, registrar e reconstruir a linha temporal, aplicar rollback dentro do contrato declarado, localizar divergencias e produzir evidencias verificaveis por uma fronteira de integracao controlada.

Renderer, editor, IA, acustica, SDF, voxel e verticais setoriais nao fazem parte do nucleo horizontal, salvo decisao normativa futura explicita.

## 3. Invariantes

1. O D-Core e a unica autoridade sobre o estado canonico.
2. Toda transicao valida ocorre por API oficial e segue `S[t+1] = f(S[t], I[t])`.
3. Inputs canonicos sao validados e ordenados antes da mutacao.
4. Consumidores externos recebem visoes imutaveis ou copias controladas.
5. Renderer, UI, telemetria, adapters e verticais nao modificam estado diretamente.
6. Relogio de parede, telemetria e budgets nao participam da decisao deterministica.
7. Rollback, serializacao, hash e evidencia usam contratos versionados.
8. Dependencias permanecem unidirecionais: host/modulo/adaptor -> D-Core.
9. Nenhum ChangeSet pode declarar capacidade concluida com lacuna interna obrigatoria conhecida.
10. Ausencia de ambiente nativo, auditoria externa ou certificacao deve ser declarada, nunca convertida em aprovacao.

## 4. Estados de claims

Somente sao permitidos: `implemented`, `verified`, `native_qualified`, `independently_audited`, `certified`, `planned`, `unsupported` e `removed`.

"Comprovado", "certificado" ou "pronto para producao" somente podem ser usados quando o ledger e a evidencia sustentarem exatamente o ambiente, corpus e alcance declarados.

## 5. Conclusao de capacidades

Uma capacidade obrigatoria somente e concluida quando satisfaz integralmente `docs/governance/PRODUCT_COMPLETION_STANDARD.md`. Classe, header, benchmark isolado ou teste unitario nao bastam.

O fluxo minimo inclui caminho ponta a ponta, erros e falha fechada, testes positivos e negativos, adversarial/fault injection quando aplicavel, recuperacao, observabilidade, documentacao, integracao, evidencia reproduzivel e verificacao independente quando houver decisao ou evidencia qualificadora.

## 6. Politica de ChangeSets

Cada ChangeSet deve declarar requisitos fechados, baseline verificavel, nao objetivos, invariantes preservados, testes, evidencias, ledgers atualizados e criterios de saida. Nao pode encerrar com gap interno obrigatorio no proprio escopo.

Pendencias somente podem permanecer como `native_qualification`, `external_assurance`, `host_responsibility`, `deployment_responsibility`, `optional_vertical`, `accepted_boundary` ou `out_of_scope`.

## 7. Estado da baseline 1.14.0

A 1.8.0 estabeleceu governanca, rastreabilidade, claims, responsabilidades, backlog finito e gates de asseguracao, preservando o nucleo canonico da 1.7.0.

O CS009, incorporado na baseline 1.9.0, fecha o escopo de evidencia ECS Y1-O2 como uma capacidade unica e verificavel: alocacao geral, arena, copy-on-write e manutencao de indices geram streams versionados, vinculados a identidade de fonte/build/configuracao e recalculados por verificador independente. Tambem reconcilia a ABI C publica 1.0 do Host SDK com o contrato interno de replay/schema do Ano 1.

A 1.9.0 **nao declara qualificacao P1**. A execucao virtualizada observada permanece `unqualified` quando amostras, ambiente nativo, termica, clocks, timing ou zero-alocacao nao satisfazem o contrato. A regressao Windows fisica recebida permanece valida para a fonte 1.7.0 identificada pelo commit `fb8362602e1b3f6530d3efe8733bc76fc6de9f3e`; ela nao e automaticamente promovida para a 1.12.0.

O CS010, incorporado na baseline 1.10.0, fecha a referencia distribuida interna: duas instancias independentes sao comparadas, divergem deliberadamente, localizam a diferenca no schema Body, recebem uma correcao opaca por UDP loopback real e convergem pela API oficial de rollback/ressimulacao. `ReplicaAdapter` estabelece a fronteira neutra de dominio sem mudar a autoridade canonica.

O modulo v1 nao e transporte remoto de producao, nao autentica nem cifra por si so e nao incorpora consenso, quorum, BFT, banco distribuido ou ordenacao multiwriter. O resultado x86_64 nao promove o claim ARM64.

O CS011, incorporado na baseline 1.11.0, rejeita o Y1-O4 como claim de runtime e remove o claim de certificado numerico global composto. O contrato aceito cobre primitivas Q32.32 com intermediario exato e rejeicao fail-closed, e separa os resultados do solver em certificado racional de pequena arvore, grade finita, residual limitado e fallback conectado operacional nao certificado. As campanhas Windows clang-cl, Linux GCC e Linux Clang foram aprovadas sobre a mesma fonte, com resultado semantico GCC/Clang byte a byte identico.

Esses resultados descrevem somente os ambientes registrados e nao promovem ARM64, desempenho universal, estimador de Lyapunov de producao, certificacao global do solver ou prova de toda composicao numerica.

O CS012, incorporado na baseline 1.12.0, fecha o contrato temporal por um recorder append-only duravel e encadeado por SHA-256, recuperacao/verificacao fail-closed, cobertura declarada de todos os campos do unico schema canonico prometido (`WorldState v1`) e budgets automaticos nos nove caminhos obrigatorios. Efeitos externos seguem prepare/confirm/commit/compensate com chave de idempotencia; rollback descarta intents preparados, mas nunca afirma desfazer efeito irreversivel ja confirmado.

As campanhas Windows clang-cl, Linux GCC e Linux Clang foram aprovadas sobre a fonte `7dc8a6f66190382adf7ee45097745d3b1c811e42`, com resultado semantico GCC/Clang byte a byte identico. O anchor externo e o armazenamento idempotente do executor continuam responsabilidades de deployment/host explicitamente limitadas. Resultados descrevem somente a fonte, ambiente e configuracao registrados e nao promovem ARM64 nem desempenho universal.

O CS013, incorporado na baseline 1.13.0, remove do produto o claim de provider assimetrico State Signature incluido e preserva somente a interface externa. O modelo de producao exige transporte confidencial autenticado e channel binding, autorizacao por comando/entidade/origem/role/chave/tempo, descritores externos de ciclo de chaves, protecao canonica de support bundles por provider AEAD e adapter explicito para anchor externo.

Providers test-only demonstram somente integracao e rejeicao fail-closed; nao provam forca criptografica, PKI, custodia, forward secrecy, nao repudio, trust externo ou auditoria independente. Esses limites permanecem responsabilidades de host/deployment ou assurance externa e nao podem ser inferidos da campanha interna.

As campanhas Windows clang-cl, Linux GCC e Linux Clang foram aprovadas sobre a fonte `28c990174742b1da1885750bc72c29a4614997a0`, com resultado semantico GCC/Clang byte a byte identico no run `30354055225`. Os resultados descrevem somente a fonte, ambientes, configuracoes e hardware registrados; nao promovem ARM64 nem uma regra de desempenho para outra maquina. O produto ainda nao esta comercialmente concluido; o proximo fechamento normativo e o CS014.

O CS014, proposto para a baseline 1.14.0, fecha a superficie publica por um mapa claim-para-SDK/ferramenta oficial, substitui a distribuicao base-mais-patches por arquivo cumulativo deterministico e exige fuzzing coverage-guided nos ingressos binarios hostis, ASan/UBSan na superficie suportada, analise estatica bloqueante, SBOM SPDX, proveniencia, manifesto e verificacao independente.

A publicacao exige atestacao externa assinada do artefato; nenhuma chave privada e incluida e um candidato local sem atestacao nao e publicavel. Entitlement permanece controlado pela distribuicao e contrato comercial, sem decisao de licenca no estado canonico. Certificacao, auditoria externa, ARM64 e qualificacao de hardware nao sao inferidas.

## 8. Regra de conflito

1. Interromper a afirmacao ou implementacao afetada.
2. Registrar o conflito no backlog.
3. Decidir implementar, reclassificar ou remover a promessa.
4. Atualizar todos os documentos e ledgers.
5. Executar os verificadores normativos e de asseguracao.
6. Retomar somente apos coerencia demonstrada.

## 9. Alteracao desta fonte

Somente por ChangeSet identificado, com justificativa, diff auditavel, impacto nos requisitos/claims, aprovacao do proprietario, verificacao por `scripts/verify_product_contract.py` e `scripts/verify_product_assurance.py`, manifesto atualizado e evidencia preservada.
