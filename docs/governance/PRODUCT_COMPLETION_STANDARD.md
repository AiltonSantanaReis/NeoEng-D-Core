# Padrao Normativo de Conclusao do Produto

Documento: `NEOENG-DCORE-COMPLETION-001`
Versao: 1.0
Baseline: 1.9.0

## Regra principal

Um requisito obrigatorio somente pode receber `complete` quando **todas** as condicoes aplicaveis abaixo estao satisfeitas e ligadas a evidencia imutavel.

1. Escopo, entradas, saidas, dominio e nao objetivos definidos.
2. Fluxo ponta a ponta acessivel pela fronteira oficial do produto.
3. Erros tipados, limites, timeouts e comportamento fail-closed.
4. Testes positivos, negativos e de fronteira.
5. Testes adversariais, fuzzing ou fault injection quando aplicaveis.
6. Recuperacao, idempotencia ou compensacao quando aplicaveis.
7. Regressao deterministica, cross-compiler e cross-architecture quando exigidas pelo claim.
8. Observabilidade correlacionavel sem interferencia no estado canonico.
9. Evidencia bruta, manifesto SHA-256, identidade de fonte/build/configuracao e ambiente.
10. Verificador independente para evidencia, qualificacao ou decisao automatizada.
11. Documentacao tecnica, contrato de host/deployment e limites publicos reconciliados.
12. Zero gap interno obrigatorio conhecido no requisito.

## Estados que nao equivalem a conclusao

- API existente sem integracao ponta a ponta;
- benchmark isolado sem dados brutos ou identidade de ambiente;
- teste unitario sem falhas negativas e recuperacao;
- resultado virtualizado usado como qualificacao nativa;
- resumo sem evidencia bruta e manifesto;
- provider abstrato apresentado como backend de producao;
- interface pronta com implementacao ou verificacao parcial.

## Gate de ChangeSet

Um ChangeSet pode ser concluido somente quando:

- a baseline de entrada e reconstruivel;
- patch e overlay produzem arvore equivalente;
- todos os requisitos selecionados atendem este padrao;
- verificadores fail-closed passam e seus autotestes provam rejeicao de adulteracoes;
- regressao obrigatoria passa nos ambientes executados;
- ambientes nao executados permanecem explicitamente pendentes;
- manifesto final cobre toda a distribuicao.

## Gate comercial final

A liberacao comercial irrestrita exige:

```text
gaps internos obrigatorios = 0
claims acima da evidencia = 0
documentos conflitantes = 0
testes obrigatorios ausentes = 0
artefatos nao reproduziveis = 0
```

Certificacao e auditoria externa nunca podem ser inferidas de testes internos.
