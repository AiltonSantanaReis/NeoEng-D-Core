# NeoEng D-Core Support Bundle v1

Schema: `neoeng.dcore.support-bundle.v1`

## Required entries

- `metadata.json`
- `traces.json`
- `evidence-chain.json`
- `deferred-validation-gates.json`
- `redaction-report.json`

Optional entries are `time-travel.json` and `visual-correlation.json`.

The manifest lists each entry path, byte size and SHA-256. `manifest.sha256` anchors `manifest.json`. The independent verifier rejects unsafe paths, duplicate paths, missing entries, undeclared files, size/hash mismatches, malformed gate schemas and redaction-policy violations.

## Security requirements

The bundle must not contain:

- session or root keys;
- authentication secrets;
- private signing material;
- raw external subject identifiers.

Subject identifiers are domain-separated SHA-256 pseudonyms truncated to 96 bits. The salt must be unique per support case/export and must never be derived from an authentication or signing key.

Time-travel export can contain input commands and state. It is included only when `time_travel_payload_authorized` is explicitly enabled by the caller.

## Integrity scope

The bundle proves the integrity of its declared entries against its manifest. A party that controls the bundle and all external anchors can rewrite both; production deployments should retain the manifest digest in an independent ticketing, WORM or notary domain.

## Reproduction

When the time-travel entry contains a complete authorized capture, the bundle may be replayable. When inputs or snapshots are omitted by policy, it remains independently verifiable but diagnostic-only.
