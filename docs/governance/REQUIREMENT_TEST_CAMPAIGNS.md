# Campanhas obrigatorias para comprovacao dos requisitos

Documento: `NEOENG-DCORE-TEST-CAMPAIGNS-001`
Baseline: 1.13.0

A definicao executavel das campanhas esta em `audit/PRODUCT_TEST_CAMPAIGNS.json`. Cada requisito aparece em pelo menos uma campanha. Um plano marcado como `planned` nao e evidencia de execucao.

## Regras comuns

Toda campanha tecnica deve preservar:

- identidade da fonte, build, dependencias, flags e configuracao;
- dados brutos e resultado recalculavel;
- manifesto SHA-256;
- testes positivos, negativos, adversariais, falhas e recuperacao conforme aplicavel;
- repeticao cross-compiler e cross-architecture quando o claim exigir;
- verificador independente e autoteste de adulteracao;
- limitacoes e ambientes nao executados declarados.

## Sequencia

- **CS009:** quatro streams ECS e qualificacao fail-closed;
- **CS010:** duas instancias, divergencia controlada, localizacao e reconciliacao;
- **CS011:** oraculos numericos, limites extremos, overflow e fallback;
- **CS012:** rollback/replay/branch/retencao/durabilidade/efeitos externos;
- **CS013:** confidencialidade, chaves, assinatura, autorizacao e bundles;
- **CS014:** fuzzing coverage-guided, sanitizers, static analysis, SBOM, assinatura, reproducibilidade e SDK;
- **CS015:** auditoria final de todos os requisitos e claims.

Campanhas nativas ARM64/P0-P4 e auditorias externas permanecem separadas e nunca podem ser inferidas de resultados internos.
