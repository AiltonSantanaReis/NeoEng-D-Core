# NeoEng D-Lab — Plano e Contrato de Validação

Baseline de origem do produto: `v1.14.1`  
Commit histórico de referência: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## 1. Papel do D-Lab

O NeoEng D-Lab é o laboratório independente de validação do NeoEng D-Core.

O D-Lab não existe para produzir PASS. Seu papel é tentar provar ou refutar, por testes reproduzíveis, que um commit específico do D-Core satisfaz os contratos definidos para aquela campanha.

O D-Lab é a fonte de verdade dos testes. O D-Core é o objeto sob teste.

Quando um teste obrigatório do D-Lab falha, o estado correto é `FAIL`. O D-Core deve ser investigado e corrigido até satisfazer o teste. O teste não pode ser alterado, removido, desabilitado, relaxado ou reinterpretado apenas para converter a falha em PASS.

## 2. Separação de autoridade

O D-Core não controla o D-Lab.

A versão do D-Lab usada em uma campanha deve ser escolhida e congelada antes da execução do D-Core. Um commit do D-Core não pode escolher uma versão mais conveniente do laboratório depois de observar um resultado.

O D-Lab deve ser mantido como unidade independente do código de produção. A implementação preferencial é um repositório próprio, com histórico e versões próprias. Enquanto essa separação física não estiver concluída, qualquer material provisório do laboratório deve ser tratado como externo à autoridade do D-Core.

## 3. Imutabilidade do laboratório

Uma versão publicada do D-Lab é imutável.

Depois que um teste entra em uma versão publicada do laboratório:

- seu conteúdo não pode ser editado;
- seu ID não pode ser reutilizado para outro teste;
- seu oracle não pode ser reduzido;
- suas assertions não podem ser removidas;
- seu timeout não pode ser ampliado apenas para esconder defeito;
- seu corpus não pode ser reduzido apenas para obter sucesso;
- sua obrigatoriedade não pode ser removida depois de uma falha;
- o teste não pode ser renomeado ou movido para escapar do inventário congelado;
- um resultado anterior não pode ser reclassificado retroativamente como PASS.

O conjunto de testes pode crescer somente por nova versão do D-Lab.

Uma nova versão pode adicionar testes ou introduzir uma correção legitimamente necessária no próprio laboratório, mas não modifica a versão anterior. A versão anterior permanece preservada e auditável.

Se for demonstrado que um teste do laboratório é defeituoso, a consequência não é aprovar o D-Core. A consequência é declarar aquela versão do D-Lab inadequada para uma nova qualificação, preservar todos os resultados existentes e criar uma nova versão do laboratório. Nenhum FAIL antigo vira PASS por causa disso.

## 4. Testes como fonte de verdade

Cada teste qualificante deve possuir identidade estável e comportamento verificável.

Um teste deve declarar, conforme aplicável:

- ID estável;
- objetivo;
- precondições;
- entrada/corpus;
- comportamento ou propriedade esperada;
- classe de falha esperada para casos negativos;
- ambiente exigido;
- se é físico, real, simulado ou híbrido;
- dependências relevantes;
- critérios objetivos de PASS e FAIL.

O teste deve observar contrato ou propriedade do produto. Quando possível, deve evitar depender de detalhes internos da implementação que não fazem parte do contrato validado.

## 5. Inventário congelado antes da execução

Toda campanha qualificante começa com um inventário fechado de testes obrigatórios.

O inventário é registrado antes do primeiro comando que executa o D-Core.

Depois do início da campanha:

- teste obrigatório não pode ser removido;
- teste obrigatório não pode virar opcional;
- `FAIL` não pode virar `SKIPPED`;
- `NOT_TESTED` não pode ser contado como PASS;
- teste novo não pode ser criado sob medida para substituir o teste que falhou.

Uma campanha deve terminar usando exatamente o inventário com que começou.

## 6. Estados de resultado

Os estados mínimos são:

- `PASS` — o teste executou e satisfez integralmente seu oracle;
- `FAIL` — o teste executou e não satisfez o oracle;
- `BLOCKED` — uma precondição impediu a execução válida;
- `NOT_TESTED` — o teste obrigatório não foi executado.

Somente `PASS` conta positivamente para validação.

`FAIL`, `BLOCKED` e `NOT_TESTED` impedem validação da campanha.

## 7. Regra de falha e correção

O fluxo obrigatório é:

`FAIL -> preservar evidência -> identificar causa raiz -> corrigir causa -> novo commit do D-Core -> nova campanha -> executar novamente o mesmo requisito`

É proibido substituir esse fluxo por:

`FAIL -> alterar teste/regra/verificador -> obter verde -> declarar sucesso`

Se a causa estiver no D-Core, corrige-se o D-Core.

Se a causa estiver no ambiente, corrige-se o ambiente e executa-se nova campanha.

Se a causa estiver no harness, corrige-se o harness em nova versão do D-Lab e executa-se nova campanha; a campanha anterior continua FAILED/BLOCKED conforme ocorreu.

Se a causa estiver no próprio teste, a versão do D-Lab é tratada como inadequada para nova qualificação e uma nova versão é criada. O resultado antigo permanece preservado e nunca é convertido retroativamente em PASS.

## 8. Identidade obrigatória de campanha

Antes do primeiro comando qualificante, a campanha registra:

- `run_id` único;
- `run_attempt`;
- commit exato do D-Core;
- versão e commit exato do D-Lab;
- inventário de testes obrigatório;
- data/hora de início;
- sistema operacional;
- arquitetura;
- compilador e versão;
- CMake/build system aplicável;
- dependências relevantes;
- configuração de build;
- identidade de hardware quando exigida.

Evidência produzida para um commit do D-Core não valida outro commit.

Evidência produzida com uma versão do D-Lab não pode ser atribuída a outra versão do laboratório.

## 9. Workspace e build qualificantes

Campanha qualificante usa workspace identificável e build produzido para aquela campanha.

Build anterior pode servir como referência ou cache somente quando isso estiver explicitamente fora do resultado qualificante. Ele não substitui a construção exigida pelo teste.

Source, build, dependências e evidência devem permanecer distinguíveis.

## 10. Evidência obrigatória

Cada comando qualificante deve preservar, conforme aplicável:

- executável;
- argumentos;
- working directory;
- horário de início e fim;
- exit code;
- stdout;
- stderr;
- classificação do resultado.

Relatórios derivados não substituem logs brutos.

A campanha deve produzir um manifesto SHA-256 das evidências relevantes para permitir detecção de alteração posterior.

## 11. Testes físicos e limites de claim

Teste que exige hardware físico deve executar em hardware físico.

GitHub Actions, VM, container, emulação ou simulação não substituem uma exigência física.

Da mesma forma:

- Windows não prova Linux;
- x86_64 não prova ARM64;
- um compilador não prova outro;
- um hardware não prova outro;
- fault injection não prova power-loss físico;
- loopback não prova rede real;
- um benchmark local não prova desempenho universal.

A conclusão é limitada exatamente ao ambiente executado.

## 12. Aleatoriedade e oracles

Teste randomizado registra seed, algoritmo/gerador e versão suficiente para reprodução.

Todo teste qualificante precisa possuir oracle, propriedade verificável ou classe esperada de rejeição. Executar algo sem saber objetivamente o que caracteriza sucesso não é teste qualificante.

## 13. D-Lab não é produto

O D-Lab pode conter harnesses, corpora, fault injection, comparadores, probes, modelos de referência e ferramentas de análise.

Nenhuma capacidade necessária ao funcionamento normal do D-Core pode existir somente no D-Lab.

O produto deve satisfazer os contratos por suas interfaces e capacidades reais.

## 14. Critério de validação do D-Core

Um commit do D-Core pode ser declarado `VALIDATED` para uma campanha somente quando:

1. o inventário de testes foi congelado antes da execução;
2. a identidade exata de D-Core e D-Lab foi registrada;
3. o ambiente exigido foi realmente executado;
4. todos os testes obrigatórios terminaram `PASS`;
5. nenhuma falha foi apagada, ignorada ou reclassificada;
6. os logs e evidências exigidos existem;
7. o manifesto de evidências confere;
8. a conclusão é limitada ao escopo efetivamente testado.

CI verde isoladamente não significa `VALIDATED`.

`VALIDATED` não significa automaticamente `RELEASED`.

## 15. Princípio final

A relação de autoridade é deliberadamente assimétrica:

`D-Lab congelado -> testa -> D-Core`

Nunca:

`D-Core falha -> adapta D-Lab -> D-Core passa`

Quando houver conflito entre conveniência de implementação e um teste obrigatório de uma versão congelada do D-Lab, o teste prevalece até que a causa seja demonstrada e tratada pelo procedimento explícito deste documento.
