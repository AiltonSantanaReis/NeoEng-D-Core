# NeoEng D-Core — Plano de Evolução Técnica

Baseline histórica de origem: `v1.14.1`  
Commit da baseline: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## 1. Objetivo

Evoluir o NeoEng D-Core a partir da baseline v1.14.1 sem reescrever o produto do zero e sem criar uma arquitetura paralela que descaracterize o núcleo existente.

O plano é técnico. Ele orienta prioridades, dependências e evidências esperadas, mas não cria uma máquina de governança sobre o desenvolvimento.

A regra de validação é externa ao produto: o D-Core deve passar nas versões congeladas do NeoEng D-Lab aplicáveis a cada fase.

## 2. Princípios permanentes

A evolução deve preservar ou versionar explicitamente os contratos existentes.

Princípios:

- a baseline v1.14.1 permanece referência histórica imutável;
- determinismo é preservado dentro dos contratos declarados;
- replay, rollback, snapshots e serialização não podem mudar silenciosamente;
- ABI e Host SDK não podem ser declarados compatíveis sem prova;
- comportamento novo não amplia claim automaticamente;
- desempenho é dependente do ambiente executado;
- falha de teste permanece falha;
- teste do D-Lab não é alterado para acomodar defeito do D-Core;
- toda conclusão de validação pertence ao SHA exato executado;
- ambientes não executados permanecem não qualificados.

## 3. Relação D-Core / D-Lab

O NeoEng D-Core é o produto sob teste.

O NeoEng D-Lab é o laboratório/oráculo de validação.

Para cada marco técnico relevante:

1. seleciona-se uma versão congelada do D-Lab;
2. congela-se o inventário de testes aplicável;
3. executa-se um commit exato do D-Core;
4. preservam-se logs, resultados e evidências;
5. qualquer FAIL bloqueia a validação daquele commit;
6. corrige-se a causa no D-Core, ambiente ou nova versão do laboratório, conforme a classificação real;
7. executa-se nova campanha completa aplicável.

## 4. Fase 0 — Reproduzir e caracterizar a baseline

Antes de melhoria de comportamento, a baseline v1.14.1 deve ser reconstruída e caracterizada em ambiente controlado.

Objetivos mínimos:

- checkout do SHA histórico;
- identificação completa do ambiente;
- build limpo;
- CTest suportado;
- determinism probe;
- Host SDK pela fronteira suportada;
- replay/rollback;
- state evidence;
- support bundle;
- hashes e manifesto;
- comparação com evidências históricas disponíveis;
- registro de diferenças de toolchain e ambiente sem extrapolação.

Saída esperada: uma referência reproduzível que permita distinguir regressão nova de característica histórica.

## 5. Fase 1 — Hardening de build e integração contínua

Objetivo: reduzir drift e ambiguidades de configuração antes de alterar contratos centrais.

Prioridades:

- consolidar opções CMake suportadas;
- rejeitar configurações críticas desconhecidas quando apropriado;
- manter Windows e Linux nas superfícies declaradas;
- documentar toolchains efetivamente suportadas;
- separar testes de produto, ferramentas de pesquisa e ferramentas de release;
- reduzir dependência implícita de ambiente;
- tornar falhas de compilação tratadas na origem, sem relaxar warnings apenas para obter verde.

Gate técnico: comportamento canônico e corpus protegido não podem regredir por causa de mudanças de build.

## 6. Fase 2 — Hardening dos contratos fundamentais

Objetivo: tornar explícitos limites que hoje possam depender de comportamento implícito.

Áreas prioritárias:

- overflow e limites de frame/tick;
- input direcionado a entidade inexistente;
- contrato numérico Q32.32 e arredondamento;
- limites e overflow de operações canônicas;
- classes estáveis de erro;
- comportamento de entradas inválidas;
- garantias de ordenação canônica;
- invariantes que precisam permanecer verdadeiros após falha.

Para cada contrato devem existir casos positivos, negativos e de fronteira no D-Lab.

## 7. Fase 3 — Corpus ouro determinístico

Objetivo: criar referência durável para detectar mudança semântica.

O corpus deve cobrir, conforme aplicável:

- entradas;
- transições;
- bytes canônicos;
- hashes;
- Merkle roots;
- snapshots;
- replay;
- rollback;
- diagnósticos/classes de erro;
- state evidence;
- Host SDK observável.

O corpus pertence ao D-Lab, não ao D-Core.

Uma vez publicado em uma versão do D-Lab, o corpus correspondente é imutável naquela versão.

## 8. Fase 4 — Verificação baseada em propriedades e modelos

Objetivo: testar relações fundamentais além de exemplos fixos.

Propriedades candidatas:

- serialize/deserialize preserva estado canônico;
- restore reproduz o estado esperado;
- rollback + replay converge para o mesmo resultado quando o contrato exige;
- ordem canônica de inputs é estável;
- implementações otimizadas permanecem equivalentes a modelos simples de referência;
- execução equivalente com estratégias internas diferentes preserva resultado canônico.

Modelos de referência e geradores pertencem ao D-Lab.

## 9. Fase 5 — Fuzzing semântico e corrupção adversarial

Objetivo: provar rejeição determinística e segura de entradas e artefatos inválidos.

Superfícies candidatas:

- snapshots;
- replays;
- checkpoints;
- state evidence;
- payloads Host SDK quando aplicável;
- formatos persistentes;
- sequências inválidas de operações.

A meta não é apenas evitar crash. A mesma classe de entrada inválida deve produzir comportamento de rejeição compatível com o contrato declarado.

## 10. Fase 6 — Desacoplamento interno do estado

Objetivo: reduzir dependência direta dos subsistemas temporais e de evidência em uma representação rígida, preservando o comportamento atual.

Prioridades:

- estabelecer contrato interno claro de estado;
- manter `WorldState v1` como referência efetiva durante a transição;
- fazer rollback, replay, snapshot, hash e evidência dependerem de interfaces internas estáveis quando isso for demonstravelmente neutro;
- provar equivalência completa no corpus antes de avançar a generalização.

Nenhuma abstração nova justifica mudança silenciosa dos bytes ou resultados protegidos.

## 11. Fase 7 — Operações canônicas e atomicidade

Objetivo: endurecer mudanças compostas de estado.

Possíveis melhorias:

- validação de lote antes de commit;
- commit atômico quando múltiplas operações formam uma unidade canônica;
- garantia explícita de ausência de estado parcial após falha;
- diagnósticos determinísticos para rejeição.

O último estado canônico confirmado deve permanecer recuperável quando o contrato exigir atomicidade.

## 12. Fase 8 — Persistência e crash consistency

Objetivo: endurecer checkpoints e persistência contra interrupção.

Inclui, conforme aplicável:

- protocolo de escrita;
- integridade;
- arquivos temporários e promoção atômica;
- flush/fsync quando a plataforma e o claim exigirem;
- recuperação do último estado válido;
- fault injection lógico;
- campanhas físicas separadas quando houver claim de power-loss físico.

Simulação nunca substitui evidência física.

## 13. Fase 9 — Identidade e compatibilidade de replay

Objetivo: impedir reinterpretação silenciosa de replay histórico.

A identidade necessária pode incluir:

- format version;
- schema ID;
- schema version;
- transition version;
- numeric contract version;
- evidence version.

Depois disso, evoluir para replay multiversão apenas quando houver regra explícita de compatibilidade e corpus histórico capaz de provar o comportamento esperado.

## 14. Fase 10 — Schema registry e generalização controlada

Objetivo: permitir evolução de schemas sem transformar o D-Core em framework de domínio.

Sequência recomendada:

1. introduzir registry interno de schemas;
2. rotear `WorldState v1` pelo registry sem alterar comportamento;
3. provar bytes, hashes, replay, rollback e Host SDK equivalentes;
4. introduzir componentes canônicos versionados somente depois da equivalência;
5. generalizar estado apenas quando existir caso técnico demonstrado.

O core não deve incorporar lógica específica de jogos, robótica, finanças ou outro setor.

## 15. Fase 11 — Diagnóstico genérico de divergência

Objetivo: localizar divergências de forma orientada pelo schema, em vez de depender de estruturas específicas.

A melhoria deve preservar a separação entre diagnóstico e autoridade canônica.

Ferramentas de visualização ou inspeção não podem modificar o estado canônico por caminhos não autorizados pelo produto.

## 16. Fase 12 — Capabilities e Host SDK

Objetivo: expor novas capacidades de forma negociável e versionada.

Prioridades:

- capability discovery explícita;
- preservação da ABI existente quando tecnicamente possível;
- versionamento explícito quando incompatibilidade for real;
- consumidores externos de teste no D-Lab;
- nenhuma dependência de headers ou estado interno fora da fronteira pública declarada.

## 17. Fase 13 — Paralelismo determinístico

Objetivo: ganhar escala sem perder determinismo.

Estratégia:

- particionamento explícito;
- trabalho paralelo sem autoridade final sobre ordem canônica;
- redução determinística;
- commit em ordem canônica;
- comparação entre diferentes números de threads.

Gate principal: execuções equivalentes com diferentes contagens de threads devem produzir o mesmo resultado canônico no escopo declarado.

## 18. Fase 14 — Large-state stress e caracterização de escala

Objetivo: medir limites reais e encontrar defeitos de escala.

Perfis candidatos:

- 1k;
- 10k;
- 100k;
- 1M, apenas quando tecnicamente viável.

Registrar:

- hardware;
- toolchain;
- configuração;
- tamanho de estado;
- duração;
- memória;
- throughput/latência quando aplicável;
- hashes/checkpoints;
- falhas.

Resultados pertencem ao ambiente medido e não autorizam claim universal.

## 19. Fase 15 — Regressão integral e nova baseline

Objetivo: somente depois das fases aplicáveis executar fechamento amplo para propor uma nova baseline publicável.

Superfície esperada, conforme o escopo final:

- Windows clang-cl;
- Linux GCC;
- Linux Clang;
- corpus ouro;
- property/model tests;
- fuzzing;
- sanitizers;
- análise estática;
- replay compatibility;
- ABI/Host SDK compatibility;
- determinism comparison;
- manifests;
- SBOM/provenance quando aplicáveis;
- reprodutibilidade no escopo declarado;
- revisão das evidências completas.

Uma nova release só pode ser proposta quando o D-Core passar no inventário obrigatório da versão congelada do D-Lab escolhida para o fechamento.

## 20. Priorização

As fases são ordenadas por dependência técnica, mas não constituem uma máquina de estados administrativa.

Uma fase posterior pode ser pesquisada isoladamente, porém nenhuma conclusão de compatibilidade ou release pode ignorar os gates técnicos das fases das quais ela depende.

O plano pode ser refinado quando surgirem novas informações, desde que isso não seja usado para reinterpretar retroativamente resultados de testes já executados.

## 21. Regra final

O plano técnico pode evoluir.

O resultado de um teste congelado não.

Quando o D-Core falhar em uma versão publicada do D-Lab, o problema deve ser explicado e corrigido. A conveniência de seguir para a próxima melhoria nunca prevalece sobre uma falha obrigatória ainda aberta.
