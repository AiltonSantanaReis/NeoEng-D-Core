# Observabilidade e time-travel debugging

## Trace correlacionável

`TraceBuffer` é um ring buffer limitado. Cada evento contém:

- correlation ID;
- sequência local;
- frame canônico;
- tempo monotônico fornecido pela integração;
- categoria, resultado e código estáveis;
- entidade e componente opcionais;
- valor medido e orçamento.

Quando o buffer enche, os eventos mais antigos são sobrescritos e o contador `overwritten_events()` é incrementado. Isso evita crescimento ilimitado. A integração deve exportar o buffer antes da perda de eventos quando a retenção for requisito contratual.

## Time-travel

`TimeTravelDebugger` implementa:

- retenção limitada de frames;
- navegação por número de frame;
- inspeção de entidade;
- comandos de input por frame;
- eventos correlacionados;
- hash de estado;
- diff semântico de identidade, posição e velocidade;
- exportação JSON reproduzível com seed, ambiente, estado, inputs e eventos.

`neoeng_time_travel_cli` demonstra a exportação. A ferramenta é textual. **Uma interface visual não foi implementada nesta etapa.**

## Integração end-to-end

`OperationalRuntime` grava:

1. autenticação ou rejeição de input;
2. avanço de estado;
3. eventos de recuperação;
4. frame resultante no time-travel.

Exceções lógicas do núcleo não são convertidas silenciosamente em sucesso. O parser exposto é total; erros internos diferentes de falta de memória continuam observáveis ao chamador.

## Limites atuais

- single-writer; sincronização multithread é responsabilidade da integração;
- o diff atual cobre o `WorldState` do Ano 1, não todos os componentes futuros;
- usa hash canônico de 64 bits existente, não uma Merkle tree por componente;
- não correlaciona GPU, driver ou timeline de renderização;
- não possui GUI, filtros interativos ou streaming remoto;
- o JSON é reproduzível, mas ainda não é um pacote assinado/comprimido;
- não há política de retenção regulatória ou tratamento de dados pessoais.

## Próximas etapas

1. schema binário versionado de trace;
2. Merkle por entidade/componente;
3. adaptadores Unreal/Unity somente leitura;
4. GUI frame a frame;
5. timeline CPU/GPU/rede;
6. assinatura do pacote reproduzível;
7. política de redaction e retenção.
