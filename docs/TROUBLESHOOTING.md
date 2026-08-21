# NeoEng D-Core — Troubleshooting

Este guia organiza falhas comuns por camada. Preserve sempre o primeiro erro útil, stdout/stderr e a identidade do ambiente antes de tentar corrigir.

## 1. Checklist inicial

Registre:

```text
D-Core SHA:
release/tag:
OS:
arquitetura:
compilador:
CMake:
Ninja:
Boost:
VCPKG_ROOT (Windows):
preset/configuração:
comando:
exit code:
primeiro erro:
```

Não comece por “tentar de novo até passar”. Primeiro classifique a causa.

## 2. CMake não encontra Boost

Verifique Boost 1.80+, package config disponível, prefixos do CMake, vcpkg no Windows e o toolchain do preset correto.

Não adicione paths privados aleatórios ao projeto apenas para mascarar a dependência.

## 3. `find_package(NeoEngDCore)` falha no consumidor

Confirme que o D-Core foi instalado e que `CMAKE_PREFIX_PATH` aponta para o prefixo correto.

O consumidor deve encontrar um package config instalado, não depender da source tree.

## 4. `NeoEng::DCoreHostSdk` não existe

Possíveis causas:

- Host SDK não foi incluído no build/instalação;
- package errado ou antigo;
- prefixo aponta para instalação diferente;
- configuração CMake incompleta.

Confirme a identidade do pacote antes de alterar o consumidor.

## 5. `ABI_MISMATCH`

Consumidor e runtime não concordam na ABI requerida.

1. leia `neoeng_dcore_host_get_version()`;
2. confirme o header utilizado;
3. confirme package/binário carregado;
4. confirme ABI major/minor;
5. não faça cast ou ignore o erro.

## 6. `WRONG_THREAD`

O mesmo handle foi acessado por thread diferente da proprietária.

Corrija a arquitetura: serialize chamadas numa thread, use fila externa ou handles independentes quando permitido. Adicionar mutex ao redor de um handle não muda automaticamente a regra de ownership.

## 7. `INVALID_ARGUMENT`

Verifique ponteiros nulos, `struct_size`, IDs, contagens, buffers, enum values e invariantes de ordenação/duplicidade quando contratados.

Preserve o input que reproduz a falha.

## 8. `INVALID_STATE`

O estado inicial ou uma transição viola invariantes.

Não corrija editando memória interna. Reduza o caso para o menor estado/input reproduzível e compare com o contrato.

## 9. `NOT_FOUND`

Comum em histórico/checkpoint fora da janela retida.

Pergunte: o frame expirou? O checkpoint foi criado? A retenção é a esperada? Houve restore/branch temporal?

Não aumente retenção silenciosamente apenas para converter o erro em sucesso.

## 10. `BUFFER_TOO_SMALL`

Use o padrão de consulta de required count, aloque e repita.

Nunca interprete ausência de output por buffer insuficiente como “lista vazia”.

## 11. `RECOVERY_REQUIRED`

Há um protocolo de recovery pendente.

1. obtenha e registre o evento;
2. execute a ação indicada pelo host/deployment;
3. envie acknowledgement com a generation correspondente;
4. confira `accepted`/reject reason;
5. só então retome operações bloqueadas pelo contrato.

## 12. Acknowledgement rejeitado

Cheque generation stale, tipo de ack incompatível, checkpoint incorreto, ação diferente da diretiva ou estado de recovery já alterado.

Uma chamada pode retornar `OK` e ainda assim o resultado semântico indicar ack não aceito.

## 13. `NUMERIC_OVERFLOW`

O cálculo Q32.32 não é representável no contrato.

Não use wraparound, saturação silenciosa ou cast para “fazer caber”. Reduza input/domínio ou trate a rejeição explicitamente.

## 14. Divergência de hash

Preserve antes de qualquer nova execução:

- estado inicial;
- inputs e ordem;
- frame;
- configuração;
- stable hash;
- canonical SHA-256;
- Merkle root;
- traces;
- binário/SHA;
- toolchain.

Depois compare as duas execuções no primeiro frame divergente.

## 15. Rollback não encontra frame

Provavelmente o frame caiu fora da retenção. Não confunda rollback funcional com armazenamento infinito.

## 16. Build Windows com clang-cl

Use Developer PowerShell e confirme:

```powershell
clang-cl --version
cmake --version
ninja --version
$env:VCPKG_ROOT
```

O preset oficial usa Ninja + clang-cl + toolchain vcpkg.

## 17. Sanitizer acusa erro

Trate a primeira mensagem do sanitizer como evidência primária.

Não desabilite sanitizer para declarar a mesma campanha aprovada, não filtre stderr e não repita até a falha desaparecer. Classifique se o defeito pertence ao produto, harness, dependência ou ambiente.

## 18. View Lab

Se o View Lab não construir, diferencie falha do companion, falha do core, dependência opcional e configuração de build.

Uma falha do View Lab não deve ser transformada em alteração da autoridade canônica.

## 19. Support bundle não verifica

O verifier pode rejeitar arquivo ausente, arquivo extra, SHA divergente, symlink, path inseguro, tamanho divergente ou manifesto adulterado.

Não edite o bundle já coletado para “corrigir” a evidência. Gere nova coleta identificada quando apropriado.

## 20. Resultado histórico parece contradizer estado atual

Consulte:

1. [`governance/DOCUMENT_STATUS_INDEX.md`](governance/DOCUMENT_STATUS_INDEX.md);
2. [`governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`](governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md);
3. [`../audit/SOURCE_OF_TRUTH_INDEX.json`](../audit/SOURCE_OF_TRUTH_INDEX.json);
4. o ledger aplicável.

Resultados históricos preservam o estado daquele momento e podem conter `pending`, `candidate` ou outra classificação anterior.

## 21. D-Lab e D-Core mostram estados diferentes

Verifique se você está comparando a mesma coisa.

- D-Core: estado/claim do produto.
- D-Lab: estado/verdict de campanha externa.
- O SHA do D-Core testado pode ser diferente da árvore pós-release atual.
- Campanha não qualificante não muda claim.
- Evidence import exige processo governado.

Não sincronize os dois projetos reescrevendo documentação para “ficar igual”.

## 22. Quando parar

Pare e classifique como bloqueado quando:

- identidade do source não pode ser provada;
- package/binário não corresponde ao esperado;
- evidência necessária está ausente;
- contrato é ambíguo;
- uma correção exigiria alterar claim/governança sem autorização;
- o erro só “some” após remover teste ou relaxar requisito.

Uma falha claramente registrada é melhor que um PASS sem provenance.
