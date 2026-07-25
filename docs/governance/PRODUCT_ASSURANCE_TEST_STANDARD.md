# Padrao de Testes e Asseguracao de Requisitos

Documento: `NEOENG-DCORE-ASSURANCE-001`
Versao: 1.0

## Objetivo

Impedir que um requisito seja fechado por uma demonstracao estreita. A matriz normativa esta em `audit/PRODUCT_ASSURANCE_MATRIX.json`.

## Classes de teste

- `positive`: fluxo nominal completo;
- `negative`: input invalido, versao incompatível e limites;
- `fail_closed`: evidencia ausente/adulterada nao pode virar aprovacao;
- `adversarial`: entradas hostis, replay, corrupcao e abuso de recursos;
- `fault_injection`: falhas de I/O, memoria, rede, dispositivo e host;
- `recovery`: restauracao, ressimulacao, truncamento e idempotencia;
- `long_run`: soak, retencao, wraparound e estabilidade termica quando aplicavel;
- `cross_compiler`: equivalencia entre toolchains homologados;
- `cross_architecture_if_applicable`: equivalencia nativa quando o claim exigir;
- `independent_verification`: recalculo sem confiar no produtor da evidencia;
- `evidence_integrity`: hashes, manifestos, identidade de fonte/build/configuracao;
- `regression`: preservacao dos invariantes e resultados aprovados;
- `reproducibility`: repeticao por procedimento documentado.

## Regra de rigor

Meta-verificadores de JSON e documentacao comprovam coerencia de governanca, **nao comprovam a capacidade tecnica**. Cada ChangeSet de capacidade deve produzir os testes tecnicos correspondentes e vincula-los no ledger.

Resultados nativos e externos somente podem ser registrados apos execucao real no ambiente exigido. Ausencia de hardware ou auditor nao e falha de implementacao, mas bloqueia o claim correspondente.

## Evidencia minima

Cada campanha qualificadora deve registrar fonte, commit ou manifesto, compilador, flags, dependencias, OS, arquitetura, hardware, configuracao, seed/corpus, amostras brutas, resultado calculado, limites, logs, SHA-256 e verificador independente.
