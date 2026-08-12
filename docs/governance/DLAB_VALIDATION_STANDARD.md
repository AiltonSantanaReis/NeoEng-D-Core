# NeoEng D-Core — D-Lab Validation Standard

Documento normativo: `NEOENG-DLAB-STANDARD-001`  
Versão normativa: 1.0  
Programa: `POST_1_14_1`  
Aplicação inicial: EV-00 / CS017  
Autorizador: CS016A

## 1. Finalidade

O D-Lab é infraestrutura independente de validação do NeoEng D-Core. Seu
objetivo não é produzir PASS; é produzir evidência suficiente para aceitar,
rejeitar ou bloquear hipóteses de forma reproduzível.

O laboratório deve tentar revelar regressões, ambiguidade, corrupção,
dependência indevida, comportamento não determinístico, falhas de integração e
lacunas de evidência.

## 2. Princípios normativos

### DLAB-R001 — Source-under-test imutável durante o run

O laboratório não modifica o checkout que está qualificando. Qualquer mudança
no produto cria novo SHA e exige novo run.

### DLAB-R002 — Product SHA obrigatório

Todo run identifica o commit exato do produto. Evidência sem SHA de produto não
é qualificante.

### DLAB-R003 — Harness SHA obrigatório

Todo run identifica a versão exata do harness e dos verificadores usados.

### DLAB-R004 — Workspace novo

Campanha qualificante inicia em workspace novo e identificável. Reutilização de
diretório não pode ocultar estado anterior.

### DLAB-R005 — Build anterior não qualifica run novo

Build, executável, cache ou install tree preexistente podem ser usados apenas
como referência/caching explicitamente não qualificante, nunca como substituto
do build do run.

### DLAB-R006 — Falha preservada

Falha relevante, output parcial e erro de infraestrutura são preservados. Não
podem ser apagados ou reclassificados como sucesso.

### DLAB-R007 — Evidência append-only após fechamento

Depois de um run atingir estado terminal, evidência bruta não pode ser
reescrita. Correções geram novo run ou novo registro append-only.

### DLAB-R008 — Verificação independente

Manifesto e evidência qualificante devem ser recalculados/verificados por
componente independente o suficiente para não confiar cegamente no mesmo
resultado que os gerou.

### DLAB-R009 — Simulação não promove claim físico

Fault injection lógico, crash simulado, loopback e emulação não provam
power-loss físico, WAN de produção, hardware não executado ou outro claim físico.

### DLAB-R010 — Ambiente não executado permanece unqualified

Nenhum resultado é extrapolado para SO, arquitetura, compilador, hardware,
driver ou topologia não executados.

### DLAB-R011 — Oracle obrigatório

Todo cenário qualificante possui output esperado, propriedade verificável ou
classe esperada de rejeição.

### DLAB-R012 — Randomização reproduzível

Campanha randomizada registra seed, algoritmo/gerador e versão.

### DLAB-R013 — Bug confirmado vira regressão permanente

Defeito confirmado gera teste/cenário de regressão quando tecnicamente possível.

### DLAB-R014 — Teste não é enfraquecido para passar

Teste, timeout, corpus, assertion, sanitizer, gate ou verificador não pode ser
relaxado para converter falha em aprovação.

### DLAB-R015 — Resultado inesperado interrompe avanço

Resultado não explicado gera `FAILED` ou `BLOCKED` e impede promoção.

### DLAB-R016 — Laboratório não é runtime

Nenhuma capacidade necessária ao funcionamento do produto pode residir somente
no D-Lab.

### DLAB-R017 — Archive é não destrutivo

Snapshots históricos, ZIPs, builds antigos, logs e evidências não são limpos,
resetados ou corrigidos retroativamente.

### DLAB-R018 — Patch exato ou nenhum patch

Patch deve passar por verificação exata; aplicação parcial/fuzzy não é padrão
aceitável.

### DLAB-R019 — Allowlist obrigatória

Cada ChangeSet operacional possui lista explícita dos caminhos autorizados.

### DLAB-R020 — Evidência não atravessa SHA

Evidência gerada para SHA A não qualifica SHA B.

### DLAB-R021 — Toda tentativa nasce identificada

Antes do primeiro comando qualificante, o run cria identidade contendo run ID,
produto, harness, objetivo e timestamps.

### DLAB-R022 — Estado terminal inequívoco

Todo run termina em exatamente um de: `PASSED`, `FAILED`, `BLOCKED`, `ABORTED`.
Pasta órfã sem estado terminal é falha do harness.

### DLAB-R023 — Source/build/install/evidence separados

O layout impede que outputs de build ou teste sejam confundidos com fonte.

### DLAB-R024 — stdout/stderr/exit code preservados

Comando qualificante registra comando/argumentos, diretório, exit code, stdout
e stderr ou mecanismo equivalente verificável.

### DLAB-R025 — Identidade de ambiente

SO, arquitetura, CPU/hardware relevante, compilador, CMake, Ninja, Boost,
dependências e configuração aplicáveis são registrados.

### DLAB-R026 — Evidência bruta e derivada separadas

Relatório derivado nunca substitui log bruto. Derivação registra suas entradas.

### DLAB-R027 — Ação exige autorização

Nenhum patch, preflight, build qualificante, campanha, avanço de stage ou
release é executado sem decisão `AUTHORIZED` do gate aplicável.

### DLAB-R028 — REJECT é bloqueante

Decisão `REJECT` ou ausência do autorizador não pode ser tratada como warning.

### DLAB-R029 — Bootstrap de sessão

Nova sessão operacional relê SOT, index, plano efetivo, roadmap, amendments e
ChangeSet antes de gerar ação.

### DLAB-R030 — Causa raiz antes de correção

Falha não é corrigida por tentativa aleatória. Deve ser classificada como
produto, teste, harness, ambiente, dependência ou governança, com evidência.

## 3. Estados de run

Estados permitidos:

- `CREATED`
- `RUNNING`
- `PASSED`
- `FAILED`
- `BLOCKED`
- `ABORTED`

Somente os quatro últimos são terminais.

`PASSED` exige que todos os gates obrigatórios do run tenham passado.

`FAILED` significa resultado técnico contrário ao esperado.

`BLOCKED` significa impossibilidade de concluir por precondição/ambiente sem
converter a ausência de teste em sucesso.

`ABORTED` significa interrupção controlada por política antes de conclusão.

## 4. Estrutura mínima de run

```text
<run>/
├── run-identity.json
├── harness/
├── source/
├── build/
├── install/
└── evidence/
    ├── raw/
    ├── derived/
    ├── verification/
    ├── terminal-state.json
    └── evidence-manifest.json
```

Implementação pode usar links/worktrees desde que as identidades e separações
continuem verificáveis.

## 5. Identidade mínima

`run-identity.json` deve registrar, quando aplicável:

- schema;
- run ID;
- stage;
- ChangeSet;
- product repository;
- product SHA;
- baseline tag;
- harness SHA;
- action authorization identity;
- branch/ref de controle;
- ambiente;
- configuração;
- toolchain;
- dependências;
- horário de criação.

## 6. Evidência por comando

Cada comando qualificante deve produzir registro contendo:

- ID;
- ação;
- executável;
- argumentos;
- working directory;
- início/fim;
- exit code;
- stdout path;
- stderr path;
- classificação do resultado.

Exit code não zero não pode ser escondido por pipeline ou comando intermediário.

## 7. Manifesto

O manifesto deve:

- usar SHA-256;
- listar todo arquivo qualificante;
- impedir duplicatas;
- registrar política de normalização quando texto for normalizado;
- não hash a si próprio;
- ser verificado independentemente;
- falhar se qualquer arquivo estiver ausente ou divergente.

## 8. Historical Assurance Revalidation

A matriz histórica contém CS001-CS015.

Para cada ChangeSet:

1. identificar documentação e commits sem inventar valores ausentes;
2. inventariar evidência;
3. reverificar hashes/manifests disponíveis;
4. avaliar ambiente original;
5. classificar risco;
6. decidir reprodutibilidade;
7. reexecutar contratos `critical/high` reproduzíveis;
8. comparar conclusão atual e histórica;
9. registrar finding append-only quando necessário.

Classificações de assessment:

- `not_assessed`
- `verified_integrity`
- `reproducible`
- `reproduced`
- `historical_only`
- `incomplete`
- `contradicted`
- `not_reproducible`
- `superseded`

Classificações de risco:

- `unclassified`
- `critical`
- `high`
- `medium`
- `low`

`unclassified` nunca é interpretado como baixo risco.

## 9. Cenários

Classes obrigatórias:

- normal;
- integration;
- degraded;
- adversarial;
- recovery;
- soak;
- combinatorial;
- regression.

Tipos:

- `real` — execução real do produto pela fronteira suportada no ambiente
  declarado;
- `simulated` — condição simulada/fault injection;
- `hybrid` — execução real combinada a condição simulada;
- `physical` — campanha física explicitamente descrita.

Cenário `physical` exige identidade do hardware e não pode ser inferido de
`simulated`.

## 10. Cenários combinatórios

O catálogo deve prever combinações de capacidades quando isso puder revelar
falhas que testes isolados não observam, incluindo, conforme aplicável:

- snapshot + rollback + replay;
- correção + restart + recovery;
- grande estado + snapshot + persistência;
- input inválido + pressão de recurso + recovery.

A inclusão no catálogo não significa que o produto já suporte todos esses
claims; aplicabilidade é verificada por estágio.

## 11. Soak e longa duração

Soak deve registrar duração planejada/real, carga, contagem de frames/operações,
memória, handles/recursos, falhas, hashes e checkpoints aplicáveis.

Ausência de campanha longa não pode ser convertida em claim long-run.

## 12. Falhas e findings

Finding possui:

- ID;
- severidade;
- fonte;
- condição reproduzível;
- evidência;
- impacto;
- owner/disposição;
- requisito/invariante afetado;
- regressão associada quando aplicável.

Finding não é apagado após correção.

## 13. Patches locais

Antes de patch:

- `HEAD` esperado;
- branch esperada;
- status;
- action authorization;
- allowlist;
- hashes/precondições quando aplicável.

Aplicação:

- `git apply --check --whitespace=error-all`;
- sem `--reject`;
- sem aplicação parcial.

Depois:

- `git diff --check`;
- `git status --short`;
- `git diff --name-only`;
- comparação exata com allowlist.

Staging nomeia explicitamente arquivos.

## 14. Comandos destrutivos

Por padrão, não autorizados em Archive ou source com material não classificado:

- `git reset --hard`;
- `git clean -fdx`;
- force push;
- exclusão recursiva de evidência;
- sobrescrita de run terminal.

Uso excepcional exige ChangeSet/ação específica e evidência.

## 15. Verificação do próprio D-Lab

O laboratório deve possuir self-tests que provem rejeição de:

- product SHA incorreto;
- harness SHA ausente/incorreto;
- source dirty;
- evidência faltante;
- evidência adulterada;
- manifesto incorreto;
- run sem estado terminal;
- múltiplos estados terminais;
- cenário sem oracle;
- simulação promovida a claim físico;
- randomização sem seed;
- build antigo usado como qualificação nova;
- caminho fora da allowlist;
- ação de etapa antes de amendment obrigatório;
- PRE-CS017 antes de CS016A aceito.

Se qualquer caso negativo for aceito, o D-Lab é `FAILED` e EV-00 é bloqueado.

## 16. Critério de confiança

Este standard não promete ausência absoluta de defeitos desconhecidos.

Ele garante operacionalmente que ausência de evidência, divergência, estado
desconhecido ou falha de governança não podem ser silenciosamente convertidos
em aprovação.
