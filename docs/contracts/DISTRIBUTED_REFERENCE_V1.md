# Contrato do módulo distribuído de referência v1

Identificador: `neoeng.dcore.distributed-reference.v1`

ChangeSet: CS010

Baseline: 1.10.0

## Finalidade

Este contrato define uma demonstração reproduzível de duas instâncias
independentes do D-Core que:

1. avançam pela API canônica;
2. comparam fingerprints versionados;
3. divergem deliberadamente;
4. localizam a divergência no adaptador D-Core;
5. transportam uma correção opaca por UDP loopback real;
6. aplicam a correção pela API oficial de rollback/ressimulação;
7. confirmam novamente a igualdade canônica.

O módulo é uma referência de integração. Não é consenso, quorum, BFT, banco
distribuído, replicação multiwriter nem transporte de produção.

## Fronteira extensível

`ReplicaAdapter` é a fronteira pública e neutra de domínio. O coordenador
enxerga apenas:

- `schema_id`;
- frame;
- SHA-256 canônico;
- payload opaco de correção;
- resultado tipado da aplicação.

`DCoreReplicaAdapter` é a implementação de referência para o schema atual
`Body/InputCommand`. Outros domínios devem fornecer outro adaptador e outro
`schema_id`; eles não podem alterar o estado canônico diretamente. Incompatibilidade
de schema ou frame interrompe a reconciliação antes de qualquer mutação.

## Formato do datagrama

Todos os inteiros do envelope são big-endian.

| Offset | Bytes | Campo |
|---:|---:|---|
| 0 | 4 | magic `NDR1` |
| 4 | 2 | versão `1` |
| 6 | 2 | tipo da mensagem |
| 8 | 8 | epoch da sessão |
| 16 | 8 | sequência |
| 24 | 8 | channel/schema id |
| 32 | 8 | frame |
| 40 | 4 | tamanho do payload |
| 44 | 32 | SHA-256 do prefixo de 44 bytes + payload |
| 76 | variável | payload opaco |

O limite padrão é 1.472 bytes por datagrama e 64 datagramas pendentes. O
payload, a fila e a retenção de correções são limitados. Não há fragmentação
implícita.

## Reconexão, replay e backpressure

- cada reconexão incrementa o `session_epoch` e reinicia a sequência local;
- epoch anterior é rejeitado como `stale_epoch`;
- sequência zero, repetida ou regressiva no mesmo epoch é rejeitada como
  `replay`;
- fila cheia retorna `backpressure` sem aceitar a nova mensagem;
- desconectado retorna `not_connected`;
- timeout, erro de socket, excesso de tamanho, envelope malformado e falha de
  integridade são resultados explícitos e fail-closed.

## Reconciliação

O coordenador:

1. compara schema, frame e SHA-256;
2. recusa schema/frame incompatíveis;
3. solicita ao adaptador autoritativo a correção do frame de input;
4. envia a correção pelo endpoint UDP;
5. valida envelope, epoch, sequência, integridade, tipo, schema e frame;
6. chama `apply_authoritative_correction`;
7. exige nova comparação convergente.

No adaptador D-Core, a única mutação ocorre por
`RollbackEngine::correct_input_and_resimulate`. Correção malformada ou fora da
janela de snapshots não altera o estado.

## Segurança e escopo operacional

O SHA-256 do datagrama detecta corrupção; ele não autentica origem e não cifra.
O v1 é limitado a loopback de referência. Dados de rede não confiáveis devem
passar pelos contratos de autenticação/sessão existentes antes de se tornarem
inputs confiáveis. Implantação remota exige política do host, autenticação,
confidencialidade e um transporte apropriado.

## Evidência obrigatória

A campanha `TEST-CS010-001` registra identidade de fonte/build, configuração,
saída bruta, resumo, manifesto SHA-256, verificação independente e limitações.
Resultados x86_64 não promovem o claim ARM64. Resultado no PC do executor
descreve somente aquele ambiente e não estabelece requisito mínimo de hardware.
