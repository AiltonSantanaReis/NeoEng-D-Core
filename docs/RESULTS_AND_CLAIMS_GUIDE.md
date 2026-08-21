# NeoEng D-Core — Como interpretar resultados, evidência e claims

## 1. Por que esta distinção existe

Um projeto de assurance perde credibilidade quando mistura implementação, teste executado, evidência, aceitação, claim pública e certificação.

No D-Core, cada camada possui escopo próprio.

## 2. Vocabulário prático

| Termo | Significado |
|---|---|
| implemented | a capacidade existe no produto dentro do escopo declarado |
| verified | há evidência para determinado corpus/ambiente/contrato |
| accepted | um gate governado aceitou um ChangeSet/release no escopo aplicável |
| native_qualified | ambiente/hardware nativo foi qualificado pelo contrato correspondente |
| independently_audited | houve assurance externa realmente independente |
| certified | certificação formal específica existe |
| planned | está planejado, não concluído |
| unsupported | explicitamente fora da superfície suportada |
| removed | claim/capacidade removida |

Os estados normativos exatos pertencem aos ledgers e à Source of Truth.

## 3. Regra fundamental: resultado é escopado

Um resultado sempre pertence a uma identidade:

```text
produto SHA
+ configuração
+ ambiente
+ toolchain
+ corpus/workload
+ comando
+ evidência
= observação reproduzível
```

Remover uma dessas dimensões reduz a força da conclusão.

## 4. Exemplos de formulação correta

### Determinismo

Adequado:

> O corpus declarado produziu resultados determinísticos nos ambientes Linux x86_64 GCC/Clang registrados.

Inadequado:

> O D-Core é deterministicamente idêntico em qualquer CPU, arquitetura e toolchain.

### Distributed reference

Adequado:

> A referência de duas instâncias foi verificada no fluxo e transporte descritos pelo contrato.

Inadequado:

> O D-Core implementa consenso distribuído de produção.

### Release

Adequado:

> A release `v1.14.1` foi aceita dentro dos claims e limitações registrados.

Inadequado:

> A release é certificada para qualquer uso crítico.

## 5. Stable hash, SHA-256 e Merkle

Esses identificadores servem para comparar estado, mas respondem perguntas diferentes de “a regra de negócio está correta?”.

Se dois estados possuem o mesmo digest sob a mesma serialização/contrato, há forte evidência de identidade daquele estado observado.

Isso não prova:

- correção do requisito;
- ausência de bugs fora do corpus;
- ausência de corrupção antes da serialização;
- qualificação de hardware não executado;
- equivalência de versão diferente.

## 6. CI verde

`success` no CI significa que aquele workflow concluiu com sucesso. Não significa automaticamente:

- ChangeSet aceito;
- release autorizada;
- claim pública ampliada;
- auditoria externa;
- certificação.

O regime corrente de validação de ChangeSets exige binding explícito de SHA/run/attempt e todos os testes obrigatórios aplicáveis.

## 7. Falhas

Uma falha é evidência. Ela não deve desaparecer porque um run posterior passou.

Ao reportar:

```text
attempt A: FAILED
causa: ...
correção prospectiva: ...
attempt B: PASS
```

é mais confiável do que publicar apenas o segundo resultado.

## 8. Simulado versus físico

Fault injection, loopback, emulação e virtualização são ferramentas úteis, mas não devem promover claim de:

- power-loss físico;
- WAN de produção;
- hardware não executado;
- thermal/power envelope real;
- certificação de dispositivo.

## 9. D-Core versus D-Lab

Um D-Lab pode produzir evidência externa de alta qualidade. Mesmo assim:

1. o D-Core continua sendo o SUT;
2. o laboratório não pode alterar retroativamente o produto testado;
3. o verdict do laboratório pertence à campanha do laboratório;
4. a governança do D-Core decide se e como essa evidência sustenta um estágio/claim;
5. nenhuma claim é importada por simples referência textual.

Assim evita-se confundir “o laboratório observou X” com “o produto agora declara X”.

## 10. Release versus desenvolvimento pós-release

A release `v1.14.1` está vinculada ao commit `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

Uma árvore posterior pode conter alterações administrativas, documentais ou técnicas. Não atribua automaticamente qualquer mudança pós-release ao pacote publicado.

## 11. Performance

Performance deve ser escrita como medição:

```text
workload W
hardware H
toolchain T
config C
resultado R
```

Evite transformar uma medição em garantia universal.

Watchdog de execução também não é threshold de performance: ele impede hang; não define SLA.

## 12. Claims públicas

A lista de claims autorizadas está em [`commercial/PUBLIC_CLAIMS.md`](commercial/PUBLIC_CLAIMS.md), gerada a partir do ledger.

Antes de publicar uma afirmação:

1. encontre a claim correspondente;
2. confirme status;
3. copie o escopo;
4. preserve as exclusões;
5. cite a evidência;
6. não extrapole ambiente.

## 13. Claims que não devem ser inferidas

A documentação atual não autoriza inferir automaticamente:

- prontidão irrestrita para produção/mission-critical;
- ARM64 equivalente à baseline x86_64;
- desempenho contratual universal;
- certificação externa;
- auditoria independente apenas porque há verifier automatizado;
- consenso/BFT/quorum;
- PKI/HSM/provider criptográfico de produção incluído;
- proteção DDoS de borda;
- adapters setoriais prontos.

## 14. Checklist de revisão

Antes de aceitar uma frase técnica, pergunte:

- qual SHA?
- qual release?
- qual ambiente?
- qual corpus?
- qual contrato?
- qual evidência?
- qual status?
- qual limitação?
- é implementação, observação, aceitação ou claim?
- o D-Core ou um laboratório externo é a fonte desse resultado?

Se qualquer resposta essencial estiver ausente, reduza a conclusão em vez de preenchê-la por inferência.
