# NeoEng D-Core — Plano Mestre Normativo de Evolução Pós-1.14.1

Documento normativo: `NEOENG-DCORE-EVOLUTION-001`  
Versão normativa: 1.0  
Produto governado: NeoEng D-Core  
Baseline histórica de origem: `v1.14.1`  
Commit da release aceita: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Estado inicial do programa: bloqueado até aceitação do bootstrap de governança `CS016`

## 1. Autoridade e precedência

Este plano é subordinado a `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`,
`audit/SOURCE_OF_TRUTH_INDEX.json`, aos ledgers normativos atuais do produto e
aos padrões de conclusão e asseguração já vigentes.

Este plano não substitui, reescreve nem retroage a baseline `v1.14.1`.
Seu objetivo é governar a evolução técnica que parte daquela baseline.

A ordem normativa aplicável é a registrada em
`audit/SOURCE_OF_TRUTH_INDEX.json`. Em qualquer conflito, o documento de maior
precedência controla. O trabalho afetado deve parar até reconciliação auditável.

O estado executável deste programa é registrado em:

- `audit/EVOLUTION_ROADMAP.json`;
- `audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json`;
- `audit/EVOLUTION_INVARIANTS.json`.

A conformidade desta governança deve ser verificada por:

- `scripts/verify_evolution_plan.py`;
- `scripts/verify_evolution_plan.py --self-test`.

Nenhuma conversa, sugestão de agente, conveniência de implementação, resumo,
memória humana ou documento de menor precedência pode autorizar uma exceção
implícita a este plano.

## 2. Objetivo final

Evoluir o NeoEng D-Core a partir da baseline comprovada `v1.14.1`, no mesmo
produto e sem arquitetura paralela, para uma infraestrutura determinística mais
robusta, verificável, desacoplada, extensível, temporalmente resiliente e
escalável, preservando:

1. autoridade canônica única;
2. determinismo dentro dos contratos declarados;
3. compatibilidade histórica explicitamente prometida;
4. rastreabilidade de requisitos;
5. evidência vinculada ao SHA exato;
6. fronteiras de integração controladas;
7. claims estritamente limitadas pela evidência;
8. releases históricas imutáveis.

O programa segue a estratégia:

`preservar -> endurecer -> provar -> desacoplar -> generalizar -> escalar`

e proíbe uma reescrita integral como atalho.

## 3. Não objetivos

Este programa não autoriza, por si só:

- transformar o D-Core em uma game engine;
- incorporar renderer, editor, IA, acústica, SDF ou voxel ao núcleo horizontal;
- incorporar lógica de domínio de jogos, robótica, finanças, defesa ou outro setor;
- criar uma segunda implementação concorrente do D-Core;
- criar uma arquitetura `v2` paralela mantida fora do runtime evolutivo;
- reescrever o produto do zero;
- alterar retroativamente o significado da release `v1.14.1`;
- reinterpretar silenciosamente replays históricos;
- quebrar a ABI C 1.x sem decisão normativa explícita;
- alegar ARM64, P0-P4, certificação, auditoria externa ou desempenho universal sem evidência correspondente;
- transformar o laboratório de validação em runtime de produção;
- remover, desabilitar ou enfraquecer testes para obter aprovação.

Qualquer necessidade real de alterar estes não objetivos exige um registro de
desvio, análise de impacto e amendment normativo antes da implementação.

## 4. Regras normativas invioláveis

### R-001 — Baseline histórica imutável

A release `v1.14.1` e seu commit
`e3fff973554a2e56b8bd7afdc1132f75f3ec337c` são referência histórica imutável.
Nenhuma alteração posterior pode ser atribuída retroativamente a essa release.

### R-002 — Uma única linha evolutiva

A evolução deve ocorrer no mesmo NeoEng D-Core. Arquitetura paralela,
reescrita geral ou produto concorrente não são autorizados por este plano.

### R-003 — Uma etapa ativa por vez

No máximo uma etapa `EV-*` pode estar `in_progress`.

### R-004 — Dependência sequencial obrigatória

Uma etapa não pode iniciar até que todas as dependências declaradas estejam
`accepted`.

### R-005 — Estados não conclusivos não liberam avanço

`not_started`, `in_progress`, `blocked`, `failed` e `superseded` não equivalem
a `accepted`.

### R-006 — Evidência vinculada ao SHA

Toda decisão de aceitação deve identificar o commit exato testado. Evidência de
um SHA não valida outro SHA.

### R-007 — Evidência de ambiente é limitada ao ambiente

Resultados de compilador, sistema operacional, arquitetura ou hardware não
podem ser promovidos para ambiente não executado.

### R-008 — Testes não podem ser enfraquecidos

Teste, gate, corpus, verificador ou condição de saída previamente obrigatória
não pode ser removido, ignorado ou relaxado apenas para permitir aceitação.

### R-009 — Regressão inexplicada bloqueia

Mudança de bytes canônicos, hash, replay, rollback, ABI, serialização,
diagnóstico determinístico ou outro resultado protegido deve ser explicada por
requisito autorizado. Sem explicação normativa, a etapa é `blocked` ou `failed`.

### R-010 — Mudança de escopo exige amendment

Implementação não prevista pelo escopo autorizado exige registro de desvio e
alteração normativa aprovada antes do código correspondente.

### R-011 — Código novo não amplia claim automaticamente

A existência de implementação não promove claim pública. Claims continuam
governadas pelos ledgers de produto e pelas evidências aplicáveis.

### R-012 — Performance não altera semântica

Otimização não pode mudar o resultado canônico. Resultado de benchmark é
dependente do ambiente e nunca prova desempenho universal.

### R-013 — Falhas são preservadas

Falhas de laboratório, fuzzing, sanitizers, regressão, verificação ou
qualificação devem ser preservadas como evidência. Não podem ser apagadas ou
reclassificadas como sucesso.

### R-014 — Ausência de evidência é ausência de aprovação

`NOT TESTED`, evidência ausente, ambiente indisponível ou verificador não
executado nunca equivalem a aprovação.

### R-015 — Implementação concluída não autoriza release

Mesmo com todas as etapas técnicas aceitas, uma release somente pode ser
autorizada após o gate final de release assurance definido neste plano e nas
políticas superiores do produto.

## 5. Modelo de três níveis

### Nível 1 — Documento normativo humano

Este arquivo define intenção, sequência, limites, critérios e processo de
desvio. Ele explica o que deve acontecer e por quê.

### Nível 2 — Ledgers legíveis por máquina

Os três ledgers de evolução registram:

- sequência e estado das etapas;
- requisitos e rastreabilidade;
- invariantes que nenhuma etapa pode violar.

O estado operacional do programa é o ledger, não uma frase solta em Markdown.

### Nível 3 — ChangeSets e evidências imutáveis

Cada etapa é executada por um ChangeSet identificado. O ChangeSet registra
baseline de entrada, escopo, não objetivos, alterações, testes e critérios.
A evidência registra o que realmente aconteceu em um SHA determinado.

Uma etapa só pode receber `accepted` quando o ledger, ChangeSet, evidência e
verificador concordarem.

## 6. Estados autorizados

Estados de etapa:

- `not_started`
- `in_progress`
- `blocked`
- `failed`
- `accepted`
- `superseded`

Estados de requisito de evolução:

- `planned`
- `in_progress`
- `verified`
- `blocked`
- `rejected`
- `superseded`

O estado `accepted` é reservado a etapas. Um requisito concluído usa
`verified`.

## 7. Bootstrap CS016

`CS016` instala esta própria governança. Ele não é uma etapa de evolução do
core e não pode alterar semântica canônica, ABI, replay, rollback, snapshots,
claims de produto ou release.

Enquanto `CS016` não estiver aceito:

- `program_state` deve permanecer `locked_pending_bootstrap_acceptance`;
- `current_stage` deve ser `null`;
- todas as etapas `EV-*` devem permanecer `not_started`;
- `release_authorized` deve ser `false`.

Após aceitação de `CS016`:

- `program_state` passa a `active`;
- `current_stage` passa a `EV-00`;
- nenhuma etapa posterior inicia automaticamente.

## 8. Contrato obrigatório de cada etapa

Toda etapa deve possuir, no ChangeSet correspondente:

1. baseline de entrada reconstruível;
2. problema ou objetivo fechado;
3. hipótese técnica quando aplicável;
4. escopo autorizado;
5. não objetivos;
6. arquivos ou superfícies autorizadas;
7. invariantes afetados e preservados;
8. implementação;
9. testes positivos;
10. testes negativos e de fronteira;
11. testes adversariais, fuzzing ou fault injection quando aplicável;
12. regressão determinística;
13. comparação com corpus ouro quando aplicável;
14. identidade de fonte/build/configuração/ambiente;
15. evidências brutas;
16. manifesto de evidências;
17. verificação independente quando a decisão depender de evidência gerada;
18. decisão explícita;
19. atualização dos ledgers;
20. gate de saída sem gap obrigatório interno.

## 9. Catálogo normativo de etapas

### EV-00 — Certificação da baseline no laboratório

Objetivo: reproduzir e registrar a baseline `v1.14.1` no laboratório autorizado
antes de qualquer alteração de comportamento.

Escopo obrigatório:

- checkout do SHA histórico;
- identidade de ambiente;
- build;
- CTest suportado;
- determinism probe;
- Host SDK;
- replay/rollback;
- state evidence;
- support bundle;
- hashes e manifesto;
- relatório de comparação com evidência histórica disponível.

Gate: nenhuma melhoria técnica pode iniciar sem uma baseline de laboratório
reconstruível e aceita.

ChangeSet previsto: `CS017`.

### EV-01 — Hardening de build, CI e governança operacional

Objetivo: remover drift de configuração e tornar validação obrigatória em
mudanças futuras.

Inclui:

- correção de opções CMake divergentes;
- CI permanente de governança e regressão apropriada;
- detecção de opções CMake desconhecidas nos pipelines críticos;
- pinning progressivo de actions/toolchains críticos;
- separação inequívoca entre validação corrente e histórica;
- modularização do build somente onde demonstrada semanticamente neutra.

Gate: corpus e comportamento canônico permanecem equivalentes à baseline
protegida.

ChangeSet previsto: `CS018`.

### EV-02 — Hardening dos contratos fundamentais

Objetivo: eliminar ambiguidades em limites fundamentais.

Inclui:

- overflow de frame;
- input para entidade inexistente;
- contrato numérico Q32.32;
- política de arredondamento;
- classes estáveis de erro;
- testes positivos, negativos e de fronteira.

ChangeSet previsto: `CS019`.

### EV-03 — Corpus ouro determinístico

Objetivo: criar oráculos imutáveis para transições, serialização, hashes,
snapshots, rollback, replay e evidência.

O corpus deve registrar entradas, saídas, bytes canônicos, hashes e diagnósticos
esperados e ser reutilizável por etapas posteriores.

ChangeSet previsto: `CS020`.

### EV-04 — Property-based e model-based testing

Objetivo: testar propriedades do sistema e comparar implementações otimizadas
com modelos simples de referência.

Propriedades mínimas incluem ordem canônica de inputs, serialize/deserialize,
restore, rollback/replay e equivalência entre estratégias onde o contrato exige.

ChangeSet previsto: `CS021`.

### EV-05 — Fuzzing semântico e corrupção adversarial

Objetivo: ir além de “não crashar” e provar rejeição determinística de estados,
replays, snapshots, checkpoints e entradas malformadas.

A mesma entrada inválida deve produzir a mesma classe de rejeição dentro do
escopo multiplataforma declarado.

ChangeSet previsto: `CS022`.

### EV-06 — Desacoplamento interno do contrato de estado

Objetivo: reduzir dependência direta de subsistemas temporais e de evidência em
`WorldState v1`, mantendo `WorldState v1` como único schema efetivo.

Rollback, snapshots, replay, hash e serialização passam a depender de um
contrato interno de estado suficientemente abstrato.

Gate principal: comportamento `v1` antes e depois deve permanecer equivalente
no corpus protegido.

ChangeSet previsto: `CS023`.

### EV-07 — Transações canônicas atômicas

Objetivo: introduzir validação e commit atômico de lotes canônicos quando o
contrato exigir múltiplas operações.

Em falha, o estado deve permanecer no último estado canônico confirmado; estado
parcial não é autorizado.

ChangeSet previsto: `CS024`.

### EV-08 — Checkpoints crash-consistent

Objetivo: endurecer persistência de checkpoint contra interrupção durante
gravação.

Inclui protocolo de escrita, flush/fsync quando aplicável, integridade, commit
atômico, recuperação e campanhas de interrupção lógica em pontos definidos.

Claims de power-loss físico exigem campanha física separada.

ChangeSet previsto: `CS025`.

### EV-09 — Identidade explícita de replay

Objetivo: tornar explícita a identidade necessária para interpretar um replay.

O contrato deve cobrir, quando aplicável:

- `format_version`;
- `schema_id`;
- `schema_version`;
- `transition_version`;
- `numeric_contract_version`;
- `evidence_version`.

ChangeSet previsto: `CS026`.

### EV-10 — Replay multiversão

Objetivo: impedir reinterpretação silenciosa de replay histórico e permitir
compatibilidade explicitamente suportada entre versões.

Gate: corpus histórico selecionado deve reproduzir resultado esperado sob a
regra de compatibilidade declarada.

ChangeSet previsto: `CS027`.

### EV-11 — Canonical Schema Registry

Objetivo: introduzir um registry de schemas no mesmo D-Core, inicialmente sem
adicionar novo domínio.

Na conclusão desta etapa, `WorldState v1` continua sendo o único schema
autorizado para estado canônico de produto.

ChangeSet previsto: `CS028`.

### EV-12 — WorldState v1 através do registry

Objetivo: rotear `WorldState v1` pelo mecanismo de registry e provar
equivalência completa.

Gate obrigatório:

- mesmos bytes canônicos;
- mesmos hashes;
- mesmos Merkle roots;
- mesmos snapshots;
- mesmos replays;
- mesmos resultados de rollback;
- mesma semântica Host SDK dentro do contrato preservado.

ChangeSet previsto: `CS029`.

### EV-13 — Componentes canônicos versionados

Objetivo: introduzir descritores de componente com IDs estáveis, versão,
ordenação e serialização canônica.

Nenhuma lógica de domínio setorial deve entrar no core.

ChangeSet previsto: `CS030`.

### EV-14 — Estado canônico genérico

Objetivo: permitir que o mesmo D-Core hospede schemas canônicos versionados sem
conhecer semântica de domínio.

`WorldState v1` deve permanecer compatível de acordo com o contrato de replay e
ABI aplicável.

Esta etapa pode exigir decisão explícita de versionamento maior se a superfície
pública não puder evoluir honestamente de forma compatível.

ChangeSet previsto: `CS031`.

### EV-15 — Localização genérica de divergência

Objetivo: tornar a localização semântica de divergência orientada por schema,
em vez de codificada somente para `Body`.

A referência distribuída continua sem promover consenso, multiwriter ou
transporte remoto de produção.

ChangeSet previsto: `CS032`.

### EV-16 — View Lab diagnóstico orientado por schema

Objetivo: permitir inspeção genérica por descritores de schema, preservando a
regra de que View Lab não possui autoridade para modificar estado canônico.

ChangeSet previsto: `CS033`.

### EV-17 — Evolução de capabilities e Host SDK

Objetivo: expor capacidades novas com negociação explícita e preservar ABI
existente quando tecnicamente possível.

Qualquer incompatibilidade real deve ser tratada por versionamento explícito,
nunca escondida sob uma ABI supostamente compatível.

ChangeSet previsto: `CS034`.

### EV-18 — Paralelismo determinístico

Objetivo: permitir trabalho paralelo com particionamento, redução e commit
canônicos.

Gate mínimo: execuções equivalentes com diferentes contagens de threads devem
produzir o mesmo resultado canônico no corpus declarado.

ChangeSet previsto: `CS035`.

### EV-19 — Large-state stress e qualificação de escala

Objetivo: caracterizar comportamento em 1k, 10k, 100k e, quando viável,
1M entidades/itens equivalentes do schema de teste.

Performance é diagnóstica até que um perfil de hardware e uma campanha
qualificadora específica sejam aceitos.

ChangeSet previsto: `CS036`.

### EV-20 — Regressão integral e release assurance

Objetivo: executar fechamento do programa evolutivo.

Deve incluir, quando aplicável ao escopo final:

- Linux GCC;
- Linux Clang;
- Windows clang-cl;
- corpus ouro;
- property/model tests;
- fuzzing;
- sanitizers;
- análise estática;
- replay compatibility;
- ABI compatibility;
- determinism comparison;
- manifest;
- SBOM;
- provenance;
- reprodutibilidade;
- atestação externa definida pela política vigente;
- verificação independente.

Somente este gate pode propor uma nova baseline publicável.

ChangeSet previsto: `CS037`.

## 10. Regras de aceitação de etapa

Uma etapa pode ser `accepted` somente quando:

- todas as dependências estão `accepted`;
- o ChangeSet existe e corresponde à etapa;
- o SHA candidato é identificado;
- requisitos obrigatórios da etapa estão `verified`;
- evidências obrigatórias existem;
- o manifesto de evidências verifica;
- testes obrigatórios passam;
- falhas e ambientes não executados permanecem declarados;
- invariantes afetados foram reconciliados;
- nenhum teste anterior foi enfraquecido sem amendment;
- o verificador de evolução aceita o estado;
- verificadores superiores aplicáveis continuam aceitando o produto.

A aceitação de uma etapa não autoriza automaticamente a próxima. A próxima
passa apenas a ser elegível para iniciar.

## 11. Regra de alteração do plano

Mudança no objetivo, ordem, dependência, não objetivo, invariante, requisito,
gate ou etapa exige amendment normativo.

O amendment deve:

1. identificar o desvio;
2. registrar a causa;
3. comparar plano original e proposta;
4. avaliar impacto em requisitos, claims, ABI, replay, evidência e releases;
5. atualizar este documento e ledgers;
6. incrementar a versão normativa deste documento;
7. executar `scripts/verify_evolution_plan.py`;
8. executar `scripts/verify_evolution_plan.py --self-test`;
9. preservar a versão anterior no histórico Git;
10. retomar o trabalho somente após reconciliação.

## 12. Processo formal de desvio

Quando uma etapa não puder ser executada como especificada:

`STOP -> deviation record -> impact analysis -> amendment -> verification -> resume`

O registro deve ser criado em `docs/records/evolution/DEV-NNNN.md` e conter:

- problema descoberto;
- requisito ou etapa afetada;
- plano original;
- motivo técnico;
- alternativas consideradas;
- decisão;
- riscos;
- impacto em compatibilidade;
- impacto em claims;
- documentos alterados;
- evidência disponível;
- autorização.

Improvisação não registrada é proibida.

## 13. Fluxo operacional obrigatório

Antes de qualquer alteração evolutiva:

1. ler `NEOENG_DCORE_SOURCE_OF_TRUTH.md`;
2. ler `SOURCE_OF_TRUTH_INDEX.json`;
3. ler este plano;
4. executar `verify_evolution_plan.py`;
5. executar o self-test do verificador quando o próprio sistema de governança for alterado;
6. identificar `current_stage`;
7. ler o ChangeSet autorizado;
8. confirmar baseline e escopo;
9. alterar somente o necessário;
10. executar testes e campanhas previstos;
11. preservar evidências;
12. verificar evidências;
13. registrar decisão;
14. atualizar ledgers;
15. executar novamente todos os verificadores aplicáveis;
16. somente então tornar a etapa seguinte elegível.

## 14. Laboratório

O laboratório é infraestrutura de validação independente do runtime. Ele pode
conter:

- baselines;
- campanhas;
- corpora;
- fault injection;
- comparadores;
- relatórios;
- ferramentas de análise.

O laboratório não pode introduzir comportamento necessário à execução do
produto. Se uma capacidade for necessária em runtime, ela deve existir na
superfície de produto apropriada e ser testada pela fronteira oficial.

Estrutura recomendada:

```text
lab/
├── baseline/
├── campaigns/
├── corpora/
│   ├── golden/
│   ├── malformed/
│   ├── replay/
│   └── snapshot/
├── comparison/
└── reports/
```

A estrutura física pode evoluir desde que a separação de responsabilidade seja
preservada.

## 15. Regra de release

Durante EV-00 a EV-19:

`release_authorized = false`

EV-20 pode produzir um candidato de release, mas publicação continua sujeita às
políticas de release assurance, final acceptance, claims e gates externos
vigentes no produto.

Nenhuma etapa deste plano converte automaticamente:

- teste interno em auditoria externa;
- execução virtualizada em qualificação nativa;
- integração em certificação;
- benchmark local em desempenho universal;
- arquitetura de aplicação em prontidão setorial.

## 16. Critério de encerramento do programa

O programa somente pode ser encerrado quando:

- EV-00 até EV-20 estão `accepted`;
- todos os requisitos obrigatórios estão `verified`;
- nenhum invariante ativo está violado;
- desvios estão reconciliados;
- replays históricos prometidos continuam verificáveis;
- ABI é compatível ou versionada explicitamente;
- claims públicas foram reconciliadas;
- evidência final é reproduzível e verificável;
- release assurance aplicável passa;
- o ledger registra decisão final sem lacuna interna obrigatória.

Até lá, o programa permanece evolutivo e não deve ser apresentado como
concluído.
