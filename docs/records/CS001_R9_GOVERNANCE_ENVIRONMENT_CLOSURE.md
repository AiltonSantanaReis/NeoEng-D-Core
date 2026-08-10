# Fechamento de CS001, R9, governança e ambiente

Documento: `NEOENG-DCORE-RECONCILIATION-20260809`
Data: 2026-08-09
Baseline observada: `f11c28e127e86f447e0ad136e038f0147b352df0`
Versão normativa do produto: 1.2
Versão da baseline: 1.14.0

## 1. Escopo e regra de decisão

Este registro fecha apenas os achados de reprodutibilidade e governança que
podem ser verificados com segurança no host local e no GitHub. Não promove
qualificação de hardware, assurance externa, certificação, ARM64, long-run,
power-loss ou desempenho universal. Esses itens permanecem deferidos para o
lançamento e para contratos que os exigirem.

O relatório histórico e o oráculo histórico CS001 permanecem imutáveis como
evidência. Nenhuma falha foi removida, comentada ou transformada artificialmente
em aprovação.

## 2. CS001-AUD-004 — line endings

**Estado:** `corrigido_comprovado` no escopo de reprodutibilidade da árvore Git.

`.gitattributes` aplica `* text=auto eol=lf` e mantém a subárvore de evidências
como `-text`. Foram criados dois clones temporários da mesma revisão, um com
`core.autocrlf=false` e outro com `core.autocrlf=true`. Os arquivos de código,
documentação e ledgers amostrados receberam `eol=lf` e produziram hashes
idênticos nos dois clones. A revisão e a configuração de atributos foram
verificadas com `git check-attr`.

Isso elimina a conversão silenciosa de line endings como causa de divergência
entre checkout Windows e Unix. Não altera bytes de evidências marcadas como
imutáveis.

## 3. CS001-AUD-005 — pacote versus Git

**Estado:** `corrigido_comprovado` para a árvore atual.

| Verificação | Resultado |
|---|---:|
| Commit observado | `f11c28e127e86f447e0ad136e038f0147b352df0` |
| Arquivos rastreados, excluindo `MANIFEST.sha256` | 699 |
| Entradas do `MANIFEST.sha256` | 699 |
| Caminhos rastreados ausentes | 0 |
| Caminhos extras no manifesto | 0 |
| Hashes de arquivos divergentes | 0 |

A divergência do pacote histórico permanece registrada como fato histórico;
ela não é sobrescrita nem usada para alterar a interpretação da árvore atual.

## 4. CS001-AUD-006 — cobertura documental

**Estado:** `corrigido_comprovado` no contrato atual; cobertura universal não é
declarada.

As afirmações atuais foram ligadas a implementações, testes e evidências em
`docs/changesets/001/CS001_AUDIT_RECONCILIATION.md`, nos ledgers de
`audit/` e nos verificadores normativos. Os verificadores executados foram:

- `scripts/verify_product_contract.py` — 36 requisitos, 20 claims e 41 limitações;
- `scripts/verify_product_assurance.py` — 36 requisitos cobertos por 10 campanhas;
- `scripts/verify_release_assurance.py` — resultado `passed`;
- `scripts/verify_consolidated_release.py --self-test` — resultado `passed`.

As limitações de campanha, hardware e assurance externa continuam descritas e
não são convertidas em claims de produção.

## 5. Oráculo histórico CS001

O oráculo histórico continua com **107 verificações e 11 falhas**. Essas falhas
permanecem evidência de compatibilidade histórica e não são mascaradas.

O oráculo migrado para o contrato vigente, mantido no laboratório externo,
passou **107/107**. A migração alterou somente fixtures de contrato e a
instrumentação opcional de contagem; não alterou a implementação do produto nem
removeu testes históricos.

## 6. R9 — imutabilidade da release

**Estado:** `proteção_corrigida_para_novas_releases`; os assets da tag histórica
`v1.14.0` não possuem imutabilidade no GitHub. Isso é uma propriedade da
release/tag e não altera a aceitação técnica da baseline horizontal `1.14.0`.

A API do GitHub confirmou que a release `v1.14.0` existente tem
`isImmutable=false`. A proteção de releases imutáveis foi habilitada no
repositório em 2026-08-09. A partir dessa habilitação, novas releases terão tag
e assets protegidos pelo GitHub.

Não foi apagada, movida ou reescrita a tag `v1.14.0`, pois isso destruiria a
proveniência histórica. Seus assets não devem ser apresentados como imutáveis. A
próxima publicação comercial deve ser criada somente após a proteção estar
ativa e deve registrar a atestação nativa do GitHub junto da proveniência
Sigstore/Cosign já exigida pelo contrato.

## 7. Governança e ambiente

R7 e R8 não aparecem como requisitos normativos definidos na fonte de verdade
v1.2. Portanto não são declarados aprovados nem reprovados. A lacuna foi
resolvida documentalmente como `não definido na baseline; nenhuma campanha
autorizada`, impedindo que um gate inexistente seja inventado.

O contrato de ambiente agora é tratado como identidade de reprodução: commit,
árvore, sistema, compilador, configuração, manifesto, hashes e comandos. Isso
é separado de qualificação de hardware. Resultados locais pertencem somente ao
ambiente registrado; máquinas inferiores ou superiores podem produzir
resultados melhores ou piores.

## 8. Regressão final não destrutiva

No build local da baseline foram executados **92/92 testes**, incluindo os
verificadores de governança, release assurance, SDK, segurança, recuperação,
evidência, fuzz smoke e integração do View Lab. Não foram executados
desligamento, power-loss, stress térmico, ARM64 ou qualquer teste que ofereça
risco ao PC.

## 9. Estados fora deste fechamento

Continuam deferidos, sem defeito de implementação declarado:

- R4 long-run formal;
- R5 perda de processo/power-loss;
- R6 ARM64;
- qualificação de hardware e desempenho P0–P4;
- assurance externa e certificações (`LIM-040`/`LIM-041`).

Este registro fecha CS001-AUD-004/005/006, a reconciliação de governança e o
contrato de ambiente. R9 fica protegido para a próxima release, enquanto a
release histórica `v1.14.0` permanece corretamente classificada como sem
imutabilidade de assets no GitHub.
