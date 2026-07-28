# ChangeSet 010 — determinismo ponta a ponta e referência distribuída

Baseline de entrada: 1.9.0

Baseline de saída: 1.10.0

Requisitos: `DCORE-DIST-001`, `DCORE-NET-002`

Limitações: `LIM-005`, `LIM-020`, `LIM-023`

Campanha: `TEST-CS010-001`

## Decisão

O CS010 entrega um módulo companheiro oficial, unidirecional e fora do estado
canônico. Ele combina:

- transporte UDP loopback real com envelope versionado e integridade SHA-256;
- fila limitada, backpressure, timeout, reconnect por epoch e anti-replay;
- `ReplicaAdapter` neutro de domínio e payload opaco;
- adaptador D-Core Body/InputCommand;
- coordenador de duas instâncias com comparação fail-closed;
- localização semântica do schema Body já suportado;
- correção exclusivamente por rollback/ressimulação oficial;
- probe longo e testes positivos, negativos, adversariais, de falha e recovery.

## Invariantes preservados

1. `neoeng_dcore` não depende do módulo.
2. Socket, fila, epoch e sequência não entram no estado canônico.
3. O coordenador não modifica componentes diretamente.
4. Schema ou frame incompatível interrompe a operação.
5. Sucesso de envio não é aceito como convergência.
6. O Host SDK C 1.0 não é alterado.

## Critérios de saída

- build e testes do módulo em Windows x86_64 clang-cl;
- campanha longa de 4.096 frames com divergência, localização e reconciliação;
- matriz GCC/Clang x86_64 para o módulo e comparação do resultado semântico;
- adulteração, replay, epoch obsoleto, excesso de payload, backpressure,
  desconexão/reconexão, correção inválida e retenção expirada exercitados;
- verificador independente e autoteste de adulteração;
- manifesto SHA-256 e limitações preservados;
- verificadores normativos e regressão proporcional verdes.

## Limites que permanecem

- UDP v1 é referência loopback, não transporte remoto de produção;
- integridade sem autenticação/confidencialidade não substitui o gateway seguro;
- x86_64 não prova ARM64;
- não há consenso, quorum, BFT, banco distribuído ou coordenação multiwriter;
- números observados pertencem ao hardware/configuração registrados e podem ser
  melhores ou piores em outra máquina.
