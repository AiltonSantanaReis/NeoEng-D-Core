# ChangeSet 015 — final product acceptance

Baseline under acceptance: `1.14.0`

Campaign: `TEST-CS015-001`

State: accepted

## Scope

- audit all 36 requirements, 20 claims, 41 known limitations and 10 test
  campaigns against the normative source of truth;
- require zero open internal limitations;
- require every mandatory requirement except the acceptance requirement itself
  to be complete and evidenced before the closure-candidate run;
- prove that generated public material contains no planned, unsupported,
  removed or prohibited claim;
- re-run the complete supported CTest surface on Windows clang-cl, Linux GCC
  and Linux Clang from one clean commit;
- independently re-verify the immutable CS014 release-assurance evidence;
- produce source/build/configuration identity, raw results, limitations,
  SHA-256 manifest and an independent acceptance verifier;
- close `DCORE-ACCEPT-001` only after immutable candidate evidence exists.

## Meaning of acceptance

Acceptance applies to the NeoEng D-Core horizontal product baseline 1.14.0
within the exact scopes and exclusions generated from the claims ledger. It
does not authorize a claim of unrestricted production readiness and does not
infer certification, independent external audit, ARM64 equivalence, P0–P4
qualification, sector readiness or performance on another machine.

## Exit criteria

- Windows clang-cl, Linux GCC and Linux Clang supported-surface regressions
  pass from the same source commit;
- every normative verifier and negative self-test passes;
- the CS014 release-assurance package still verifies independently;
- the CS015 evidence package verifies independently with no missing,
  additional or modified manifested file;
- zero internal mandatory gaps and zero unsupported public claims are reported;
- deferred native and external gates remain explicit and nonblocking for
  CS015, without being promoted;
- the final requirement, campaign, reports and public status are reconciled
  without changing the technical baseline version.

## Acceptance record

All exit criteria passed for source
`0d909e75380140078e84d1b5af07ed1e709082b5` in GitHub Actions run
`30375982639`. The independently verified package is recorded at
`docs/changesets/015/evidence/github-actions-run-30375982639`.

The final decision closes `DCORE-ACCEPT-001` and accepts the horizontal
baseline 1.14.0 only within generated public claims and recorded limitations.
Native and external gates remain separate and no prohibited claim is promoted.
