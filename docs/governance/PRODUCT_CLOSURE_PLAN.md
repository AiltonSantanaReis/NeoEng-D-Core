# NeoEng D-Core - Plano Fechado de Conclusao

Documento: `NEOENG-DCORE-CLOSURE-PLAN-001`
Versao: 1.2

O plano fecha o NeoEng D-Core como produto independente. Nao inclui o programa Deep Tech de cinco anos nem renderer, editor, IA, acustica, SDF ou voxel no nucleo horizontal.

## CS008 - Contrato, rastreabilidade e baseline de asseguracao — CONCLUIDO EM 1.8.0

Fonte normativa, padrao de conclusao, matriz de responsabilidade, 36 requisitos, 20 claims, 41 limitacoes, matriz de asseguracao, verificadores fail-closed e regressao do nucleo preservado.

## CS009 - Fechamento integral do escopo ECS Y1-O2 — CONCLUIDO EM 1.9.0

Alocacao geral, arena, copy-on-write e manutencao de indices foram fechados como uma capacidade unica de evidencia, com streams brutos versionados, identidade de fonte/build/configuracao, verificador independente, autotestes de adulteracao e decisao fail-closed. A qualificacao nativa P1 continua separada e `UNQUALIFIED` ate satisfazer hardware, amostras, termica, clocks, timing e zero-alocacao.

## CS010 - Determinismo ponta a ponta e referencia distribuida — CONCLUIDO EM 1.10.0

Duas instancias independentes, mesmos inputs, comparacao, divergencia controlada, localizacao, reconciliacao e evidencia. Transporte/coordenador de referencia fora do estado canonico; consenso, quorum e BFT fora do escopo.

## CS011 - Fechamento numerico — CONCLUIDO EM 1.11.0

Y1-O4 rejeitado como claim de runtime; certificado global composto rejeitado; contrato fail-closed de primitivas Q32.32 e classificacao explicita dos dominios/certificados/fallback do solver de arvores obliquas. Campanhas Windows clang-cl, Linux GCC e Linux Clang aprovadas, com resultado semantico GCC/Clang byte a byte identico e sem inferir ARM64 ou desempenho universal.

## CS012 - Fechamento temporal e efeitos externos — CONCLUIDO EM 1.12.0

Recorder temporal append-only duravel e encadeado por SHA-256, recuperacao e adulteracao fail-closed, branch/retencao explicitas, protocolo idempotente prepare/confirm/commit/compensate, reconciliacao de rollback e instrumentacao automatica dos caminhos obrigatorios. Campanhas Windows clang-cl, Linux GCC e Linux Clang aprovadas, com resultado semantico GCC/Clang byte a byte identico. O anchor externo e o executor idempotente real permanecem responsabilidades declaradas de deployment/host; ARM64 nao foi inferido.

## CS013 - Seguranca e evidencia criptografica de producao — CONCLUIDO EM 1.13.0

O claim de provider assimetrico State Signature incluido foi removido. O contrato de producao exige transporte confidencial autenticado e channel binding, autorizacao granular deny-by-default, descritores externos de ciclo de chaves, protecao canonica de support bundles por provider AEAD e adapter de anchor externo. Campanhas Windows clang-cl, Linux GCC e Linux Clang aprovadas sobre a mesma fonte, com resultado semantico GCC/Clang byte a byte identico; ARM64, provider criptografico de producao, PKI, custodia, trust externo e auditoria independente nao foram inferidos.

## CS014 - Release assurance e SDK completo — CONCLUIDO EM 1.14.0

Mapa claim-para-SDK/ferramenta oficial, fuzzing coverage-guided, sanitizers, analise estatica bloqueante, SBOM SPDX, proveniencia, duas atestacoes keyless Sigstore publicas, CI reproduzivel, pacote cumulativo consolidado e politica comercial fora do estado canonico. Campanha Windows clang-cl, Linux GCC/Clang e verificacao independente aprovadas no run `30367653644`; ARM64, certificacao, auditoria externa e desempenho universal nao foram inferidos.

## CS015 - Aceitacao final — CONCLUIDO NA BASELINE 1.14.0

Auditoria integral dos ledgers, regressao Windows clang-cl e Linux GCC/Clang, reverificacao independente do CS014, zero gap interno obrigatorio e publicacao de claims estritamente sustentados. A evidencia imutavel do run `30375982639` aceita o produto horizontal na baseline 1.14.0 sem inferir prontidao irrestrita, ARM64/P0-P4, certificacao, auditoria externa ou desempenho em outra maquina.

Uma revalidacao corretiva posterior, no run `31246260738` sobre a fonte
`2348f147452e8183a62f54db34dd3cf46388f28d`, confirmou 54/54 testes em
Windows clang-cl, Linux GCC e Linux Clang, verificou a atestacao externa de
proveniencia com Sigstore/Cosign e passou o verificador fail-closed. Essa
revalidacao corrige a cadeia de autenticacao da evidencia sem alterar a
baseline 1.14.0 nem ampliar claims de hardware, desempenho ou prontidao.

## Estado pós-release da baseline 1.14.0 — CANDIDATO NÃO PUBLICADO

O release público `v1.14.0` permanece imutável no commit
`488112e9e1a248686eff168c453cb51915f72498` e não contém as correções
posteriores do laboratório. Os commits `8d994c4` e `ef8d628`, incorporados no
merge `2fe59dac`, corrigem achados funcionais posteriores e foram revalidados
localmente em Windows x86_64 clang-cl. Essa árvore é candidata a uma nova
distribuição, não uma atualização silenciosa do arquivo `v1.14.0`.

Antes do fechamento comercial dessa correção, é obrigatório definir a nova
identidade de versão, repetir o contrato de assurance no commit candidato,
regenerar manifesto/SBOM/proveniência, verificar reprodutibilidade e obter as
atestações externas exigidas.


## Published baseline 1.14.1 — CS014 and CS015 accepted

The candidate branch `agent/release-1.14.1-candidate` has its own CS014
assurance evidence. Commit `143163833764f92beb84646674d845bc82f7ab24` passed
GitHub Actions run `31381564124` across Linux GCC/Clang, Windows clang-cl,
ASan/UBSan, coverage-guided fuzzing, blocking clang-tidy, deterministic
packaging and public Sigstore verification. The candidate archive digest is
`70136e7cd71e652d3c7a8fcf60cd0286b5de81ea460e60fdf9268b9a79631fb6`.

CS015 then passed in run `31384684512` on commit
`880a1820b570ec1c9ebb2892206068fbcf7bd1ef`: Linux GCC, Linux Clang and
Windows clang-cl passed; the fail-closed evidence assembly, public Sigstore
attestation, attestation verification and independent evidence verifier all
passed. The immutable campaign evidence is
`docs/changesets/015/evidence/github-actions-run-31384684512` and contains
19 files with 15 manifest entries. Its recorded state is `closure_candidate`
because the fail-closed state machine only promotes `DCORE-ACCEPT-001` after
that package is registered; the current ledgers and
`audit/FINAL_ACCEPTANCE_VALIDATION.json` now report `accepted` with zero open
mandatory requirements and limitations.

This evidence belongs only to `1.14.1`; it does not rewrite or retroactively
validate the immutable `v1.14.0` release. The release was published at tag
`v1.14.1` on merge `e3fff973554a2e56b8bd7afdc1132f75f3ec337c` after the main
CS014 run `31387419484` and main CS015 run `31387421705` passed. The published
archive SHA-256 is
`107c8c4f90642151648de523e6b461500c6dc90915f759f930490c03e29f53bf`.
Deferred hardware, external assurance, certification and deployment gates
remain governed by the ledger.