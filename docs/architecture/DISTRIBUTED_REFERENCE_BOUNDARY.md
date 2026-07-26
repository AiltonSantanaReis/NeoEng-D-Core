# Fronteira do módulo distribuído de referência

## Regra de dependência

```text
host/campanha
    |
    v
TwoInstanceCoordinator + UdpReferenceEndpoint
    |
    v
ReplicaAdapter (schema e payload opacos)
    |
    +--> DCoreReplicaAdapter --> NeoEng::DCore
```

Nunca existe dependência no sentido `D-Core -> distributed_reference`.

O socket, a fila, epoch, sequência, timeout, relógio de parede e telemetria
ficam fora do estado canônico. O módulo não adiciona campos a `WorldState`, não
altera `S[t+1] = f(S[t], I[t])` e não recebe ponteiros mutáveis para o estado.

## Autoridade

- a instância autoritativa exporta apenas uma correção de input já registrada;
- a instância seguidora valida o formato e usa rollback/ressimulação;
- o coordenador não edita `Body`, snapshot ou histórico internamente;
- a igualdade final é decidida por fingerprint canônico, não por sucesso do
  transporte;
- divergência persistente produz `reconciliation_mismatch`.

## LIM-020

A especialização pública existente do Host SDK C em `Body/InputCommand` não é
transformada artificialmente em ABI genérica no CS010. O fechamento correto é
uma fronteira C++ companheira e neutra, `ReplicaAdapter`, com payload opaco e
identificador explícito de schema. O adaptador Body é uma implementação, não o
contrato horizontal.

A exposição integral dessa e de outras capacidades em SDKs distribuídos
permanece governada por `DCORE-SDK-002`/CS014.

## Não objetivos

- consenso, eleição de líder ou tolerância bizantina;
- ordenação global multiwriter;
- armazenamento distribuído;
- sincronização de efeitos externos irreversíveis;
- transporte remoto pronto para produção;
- inferência de equivalência ARM64;
- alteração da ABI C 1.0 do Host SDK.
