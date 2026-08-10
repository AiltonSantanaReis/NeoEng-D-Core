# CS001 — reconciliação das pendências AUD004, AUD005 e AUD006

**Estado:** correção técnica verificada na baseline `f11c28e`; registro publicado
como adendo de reconciliação. A release histórica `v1.14.0` continua
explicitamente não imutável.
**Data da rodada:** 2026-08-09.

Este documento é um adendo de auditoria. O relatório histórico e o oráculo
original permanecem preservados. Nenhuma falha foi removida, comentada ou
reinterpretada como aprovação.

## AUD004 — line endings e aplicação byte-estável

### Achado histórico

Um checkout Windows com `core.autocrlf=true` convertia arquivos textuais sem
atributo explícito para CRLF. Isso podia fazer um verificador byte-addressed
rejeitar um pacote apesar de a aplicação semântica do patch estar correta.

### Correção aplicada

`.gitattributes` agora define:

```gitattributes
* text=auto eol=lf
docs/changesets/**/evidence/** -text whitespace=-trailing-space
```

A segunda regra continua protegendo evidências como bytes imutáveis. A primeira
estabiliza código, documentação e metadados em LF nos checkouts Windows e Unix.

### Verificação

Em clone temporário com `core.autocrlf=true`, `README.md` apresentou 275 LF e
zero CRLF após o checkout com a regra aplicada. A evidência externa está em
`C:\Users\atnco\NeoEng-Validation\Historical-Audit-CS001-CS015\12-reconciliation\stage12-cs001-closure-2026-08-09\AUD004_REPRO.md`.

O manifesto de um release deve ser regenerado depois que esta alteração for
publicada em um commit limpo. O worktree atual ainda contém alterações locais;
portanto nenhum pacote novo é declarado como release final nesta rodada.

## AUD005 — equivalência pacote–Git

O conjunto atual foi comparado independentemente com `MANIFEST.sha256`:

| Verificação | Resultado |
|---|---:|
| `git ls-files` | 700 caminhos |
| caminhos no manifesto | 699 |
| caminhos rastreados, excluindo `MANIFEST.sha256` | 699 |
| rastreados ausentes do manifesto | 0 |
| caminhos extras no manifesto | 0 |

O gerador de release usa diretamente `git ls-files`, em vez de copiar uma
árvore de trabalho ou um pacote histórico. Isso corrige a causa estrutural do
achado para releases gerados a partir de uma árvore limpa. A divergência do
pacote histórico continua registrada como evidência histórica e não foi
apagada.

## AUD006 — cobertura documental

O texto histórico de CS001 continua preservado. Este adendo torna explícita a
fronteira entre asserções dirigidas, fuzz determinístico e limitações:

| Área | Asserções dirigidas atuais | Limitação que permanece |
|---|---|---|
| gateway, HMAC, replay e limites | `tests/operational_hardening_tests.cpp` e testes independentes CS001 | não substitui campanha contínua libFuzzer/AFL++ |
| parser de payload e rejeições | `tests/operational_hardening_tests.cpp` | fuzz smoke não prova cobertura de todos os ramos |
| runtime, recovery e correlação | `tests/operational_hardening_tests.cpp` | fault injection permanece lógico |
| hardware qualification v2 | `tests/hardware_qualification_tests.cpp` | qualificação física exige campanha nativa |
| observabilidade e suporte | `tests/observability_support_tests.cpp` | não é certificação externa |
| estado/evidência e integridade | `tests/state_evidence_tests.cpp` e verificadores de manifesto | não substitui auditoria criptográfica independente |

O padrão de assurance do projeto continua normativo: meta-verificadores e
documentação demonstram coerência de governança, não capacidade técnica. Cada
claim técnico precisa de teste dirigido e evidência reproduzível.

## Oráculo histórico CS001 — 107 verificações

O executável histórico original continua registrando 107 checks e 11 falhas.
Uma cópia explicitamente versionada contra o contrato atual foi reconstruída
sem alterar a fonte histórica e passou com 107/0:

- as 8 expectativas de hardware foram atualizadas para fornecer a evidência
  completa exigida por `HARDWARE_QUALIFICATION_V2`; perfis incompletos continuam
  `unqualified`, e não são promovidos a `passed`;
- o fixture de runtime desativou somente a instrumentação opcional de budget
  para preservar a contagem histórica de eventos funcionais; o produto mantém
  essa instrumentação habilitada por padrão;
- nenhuma implementação, teste histórico ou falha foi removida.

O código e a saída da cópia atual estão no laboratório externo em
`stage12-cs001-closure-2026-08-09/CURRENT_ORACLE/`. A distinção entre o oráculo
histórico e o oráculo do contrato vigente é obrigatória para evitar uma falsa
afirmação de que os 11 resultados antigos nunca existiram.

## Estado após a verificação final

O build local da baseline `f11c28e` passou 92/92 testes. A comparação atual
pacote–Git passou com 699 entradas e zero caminhos ou hashes divergentes. Clones
temporários com `core.autocrlf=false` e `core.autocrlf=true` produziram hashes
iguais nos arquivos textuais amostrados.

O oráculo histórico continua preservado em 107 verificações/11 falhas. A cópia
do contrato atual passou 107/107 no laboratório, sem remover ou comentar testes
históricos.

R9 foi tratado sem reescrever a história: a proteção de releases imutáveis foi
habilitada no GitHub para novas publicações, mas `v1.14.0` permanece registrada
como release histórica não imutável. Hardware, assurance externa,
certificação, ARM64, long-run e power-loss permanecem deferidos conforme o
plano aprovado.
