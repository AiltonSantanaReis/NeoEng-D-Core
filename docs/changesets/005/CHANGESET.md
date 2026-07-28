# ChangeSet 005 — Observability and Reproducible Support Bundle

Base: NeoEng D-Core 1.4.0

Target: NeoEng D-Core 1.5.0

## Added

- subsystem/severity/subject/hash/detail fields in trace events;
- automatic wall-clock budget observations for authenticated ingest and state advance;
- explicit budget API for rollback, ECS, evidence, View Lab and tooling;
- stable `BudgetSampled` and `BudgetExceeded` events;
- hierarchical state-divergence diagnosis using stable hash, SHA-256, Merkle root and semantic localization;
- `neoeng.dcore.support-bundle.v1` builder and verifier;
- bounded export, SHA-256 manifest, subject pseudonymization and explicit time-travel authorization;
- independent Python verifier and Windows collection script;
- formal deferred-validation ledger distinguishing implementation, native validation, external assurance and future infrastructure.

## Compatibility

The canonical state layout, stable hash, simulation ordering and evidence-chain format are unchanged. New timing data is diagnostic metadata and never participates in deterministic decisions.
