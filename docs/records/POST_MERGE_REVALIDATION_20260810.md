# Registro de revalidação pós-merge — 2026-08-10

Documento: `NEOENG-DCORE-POST-MERGE-REVALIDATION-20260810`
Estado: `revalidado_localmente_sem_nova_release`
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

## 4. Decisão de versão

A baseline pública permanece `1.14.0`, conforme a fonte de verdade e o plano
de fechamento. As mudanças do PR #18 são classificadas como correção de
manutenção revalidada, sem novo claim e sem nova release comercial. Uma
futura distribuição deve possuir sua própria identidade de release, manifesto,
SBOM, proveniência, atestações e verificação independente; não se deve afirmar
que os artefatos imutáveis anteriores cobrem automaticamente este commit.

## 5. Limitações preservadas

Continuam fora desta revalidação: ARM64, qualificação P0–P4, long-run,
power-loss, stress térmico, assurance criptográfica externa, certificação,
infraestrutura de deployment e regra universal de desempenho para outra
máquina. Esses estados permanecem no ledger de gates deferidos.
