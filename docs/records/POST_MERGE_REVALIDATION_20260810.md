# Registro de revalidação pós-merge — 2026-08-10

Documento: `NEOENG-DCORE-POST-MERGE-REVALIDATION-20260810`
Estado: `revalidado_localmente_candidato_pos_release`
Baseline aceita: `1.14.0`
Merge commit: `2fe59dac4f71c6ab51db2e473d1a0ec3517835c5`
Commit de código revalidado: `ef8d6286051e39a21bebf2958284f3510313da52`

## 1. Escopo e regra de interpretação

Este registro comprova a revalidação local do estado incorporado em `main`
após o PR #18. Ele não substitui as evidências imutáveis do CS014/CS015, não
cria uma nova baseline e não amplia claims de hardware, desempenho,
certificação, auditoria externa ou prontidão irrestrita.

## 2. Identidade do ambiente

- sistema: Windows x86_64 do host de validação;
- compilador: clang-cl 22.1.0;
- CMake: 4.3.1-msvc1;
- Ninja: 1.13.2;
- configuração: Release, warnings-as-errors, testes e companions habilitados;
- dependências: Boost/vcpkg local já instalado, usado apenas como prefixo de
  configuração para o build limpo.

A primeira configuração sem o prefixo de dependências falhou porque o CMake
não encontrou Boost. A configuração repetida com o prefixo local conhecido
foi concluída; isso é uma condição de preparação do ambiente, não uma falha
funcional do produto.

## 3. Resultados reproduzidos

| Verificação | Resultado |
|---|---:|
| Alvos compilados no build limpo | 251/251 |
| CTest completo | 92/92 aprovados |
| Verificadores de contrato, assurance, aceite e isolamento | aprovados |
| Verificador de bundle atual | aprovado |
| Self-test do verificador de release consolidado | aprovado |
| Matriz negativa dos quatro fuzzers | 24/24 aprovados |

A matriz negativa cobriu `0`, `1x`, `abc`, overflow `uint64_t` e `-1`, além de
uma execução válida por fuzzer. O teste UAC da mesma árvore de código rejeitou
corretamente um symlink; o merge commit apenas adicionou o wrapper GitHub,
sem alterar essa árvore.

### 3.1 Reconciliação dos achados pós-release

- `CS001-AUD-004/005/006` estão descritos no registro
  `docs/records/CS001_R9_GOVERNANCE_ENVIRONMENT_CLOSURE.md`; a distinção entre
  o oráculo histórico (107 verificações e 11 falhas preservadas) e o contrato
  vigente migrado (107/107) é mantida, sem remover ou comentar o histórico.
- O commit `8d994c4` registra correções de suporte, segurança, estado e
  correlação visual, com testes negativos/adversariais correspondentes.
- O commit `ef8d628` acrescenta a validação de argumentos dos fuzzers,
  cobertura de suporte/observabilidade e testes de recuperação/estado.
- A verificação local desta árvore executou os resultados da tabela acima; isso
  comprova o ambiente Windows registrado, não qualificação de outras máquinas.

## 4. Decisão de versão

A release pública `v1.14.0` já existia antes destas correções e permanece
imutável. Ela aponta para o commit `488112e9e1a248686eff168c453cb51915f72498`
e para o arquivo publicado com SHA-256
`47cb632ee66a35c98c8ca7490958024b7f9b9e22044c0684981349dba52f3220`.

Após essa publicação, os commits `8d994c4` e `ef8d628` corrigiram achados
funcionais do laboratório em suporte, segurança, evidência de estado,
observabilidade, correlação visual, recuperação, fuzzers e testes. O merge
`2fe59dac` contém essas correções, mas não faz parte do arquivo `v1.14.0`
publicado. Portanto, esta árvore corrigida é apenas uma candidata a uma nova
release até que receba identidade, aceitação e atestações próprias.

O pacote local reprodutível gerado a partir de `origin/main` teve SHA-256
`516bfea23676d6a0713ebb02874672c7c76eceb92e3297f75a6f8e732aed320a`, passou
o verificador consolidado, mas permanece unsigned e não publicável.

Não há alteração de ABI C Host 1.0 identificada no diff entre `v1.14.0` e
`origin/main`; a compatibilidade da superfície C++ e a classificação final da
versão ainda devem ser registradas antes da publicação.

## 5. Limitações preservadas

Continuam fora desta revalidação: ARM64, qualificação P0–P4, long-run,
power-loss, stress térmico, assurance criptográfica externa, certificação,
infraestrutura de deployment e regra universal de desempenho para outra
máquina. Esses estados permanecem no ledger de gates deferidos.

## 6. Regra de publicação

Nenhum documento ou claim deve associar as correções pós-release ao arquivo
`v1.14.0`. A próxima release só poderá ser publicada após satisfazer novamente
o contrato de assurance, com manifesto, SBOM, proveniência, reprodutibilidade,
verificação independente e atestações externas do commit exato.
