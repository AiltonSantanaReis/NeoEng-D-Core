# Contrato `neoeng.dcore.state-evidence-chain.v1`

## 1. Camadas de hash

1. `stable_hash(WorldState)` — FNV-1a de 64 bits existente, destinado a determinismo e comparação rápida.
2. `canonical_state_sha256(WorldState)` — SHA-256 sobre `canonical_serialize(WorldState)` sem transformação adicional.
3. `state_merkle_sha256(WorldState)` — raiz SHA-256 estrutural por chunks.
4. `evidence_envelope_hash(StateEvidenceEnvelope)` — SHA-256 sobre o envelope canônico.

Nenhuma camada substitui silenciosamente outra.

## 2. Merkle Tree

As folhas codificam, em little-endian:

- domínio `NEOENG-DCORE-STATE-LEAF-V1`;
- versão do formato Merkle;
- índice do chunk;
- índice do primeiro corpo;
- quantidade de corpos;
- campos canônicos de cada `Body`.

Folhas de padding usam domínio distinto. Nós internos incluem domínio, versão, nível e os dois filhos. A raiz final vincula frame, versão da serialização, quantidade de corpos, tamanho do chunk, quantidade real de chunks e capacidade da árvore.

## 3. Envelope canônico

O hash do envelope inclui:

- versão do schema;
- sequência local da branch;
- frame e quantidade de corpos;
- `stable_hash`;
- SHA-256 canônico;
- Merkle Root;
- hash do envelope anterior;
- branch ID;
- hash e frame do pai da branch;
- correlation ID;
- producer ID com comprimento explícito.

`monotonic_time_ns` é deliberadamente excluído do hash canônico. Ele serve somente para ordenação operacional local e não pode ser usado como prova temporal independente.

## 4. Ramificação após rollback

Uma cadeia raiz possui `branch_parent_hash = 0` e `branch_parent_frame = 0`.

Uma cadeia derivada começa com:

- sequência 0;
- `previous_envelope_hash = 0`;
- novo branch ID não nulo;
- `branch_parent_hash` igual ao envelope validado de origem;
- `branch_parent_frame` igual ao frame do envelope pai.

Isso distingue uma nova linha temporal de reescrita silenciosa do histórico.

## 5. Assinaturas

O núcleo define interfaces `EvidenceSigner` e `EvidenceSignatureVerifier`. O material privado não pertence ao D-Core. O provedor externo recebe o hash do envelope e devolve bytes de assinatura mais identificação de algoritmo e chave.

`HmacSha256TestOnly` existe para regressão determinística e ambientes controlados. Não deve ser descrito como assinatura assimétrica, não repúdio ou prova de identidade pública.

## 6. Verificação

- `verify_state_evidence` recompõe os hashes a partir de um `WorldState` e valida opcionalmente a assinatura.
- `verify_evidence_chain` detecta schema inválido, metadados inconsistentes, sequência incorreta, remoção interna, reordenação, quebra do elo anterior, mudança de branch, envelope adulterado e assinatura rejeitada. Quando recebe `EvidenceChainAnchor`, também detecta truncamento final e anexação não autorizada pela divergência de contagem e head hash.
- `verify_state_merkle_proof` verifica um chunk sem exigir o restante do estado.

## 7. Âncora confiável

`EvidenceChainAnchor` registra branch, quantidade de records, head hash e pai da branch. A âncora precisa ser armazenada ou assinada fora do mesmo domínio de confiança da cadeia. Sem assinatura ou âncora externa, um agente que controle todo o armazenamento pode recalcular uma cadeia completa; o encadeamento isolado não fornece não repúdio.
