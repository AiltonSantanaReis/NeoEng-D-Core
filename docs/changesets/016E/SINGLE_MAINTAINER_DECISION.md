# CS016E — Single-Maintainer Governance Decision

State: `in_progress / bootstrap`

Declared maintainer: `AiltonSantanaReis`

Decision date: `2026-08-12`

## Declaration

The repository will operate with a single human maintainer. There is no independent second maintainer available to provide a genuine pull-request approval.

A second account controlled by the same person MUST NOT be used to simulate independent review. The absence of an independent human reviewer is a declared governance limitation and MUST NOT be represented as an approval.

## Normative effect

`audit/REPOSITORY_PROTECTION_POLICY.json` has higher precedence than the lower-precedence evolution amendment in `audit/SOURCE_OF_TRUTH_INDEX.json`. During the still-unaccepted CS016E bootstrap, the repository-protection policy is refined to the explicit `single_maintainer` model.

Only the human-review count is changed for this topology:

- pull requests remain mandatory;
- required human approvals are exactly `0`;
- a nonzero required approval count in single-maintainer mode is a blocking misconfiguration;
- strict required status checks remain mandatory;
- required checks are pinned to the expected GitHub Actions App identity;
- administrators remain subject to protection;
- routine pull-request bypass allowances remain empty;
- force pushes remain disabled;
- branch deletion remains disabled.

This decision does not authorize direct pushes to `main`, does not authorize bypass, does not weaken evidence requirements and does not authorize release.

## Compensating root-of-trust controls

The lack of a second human reviewer is compensated by a two-phase acceptance boundary:

1. before bootstrap merge, the exact CS016E candidate SHA must pass all internal governance gates and live repository-protection verification;
2. the bootstrap merge itself does **not** accept CS016E;
3. immediately after bootstrap merge, `Trusted governance root gate` must be configured as a required check on `main`;
4. a real post-merge main run must succeed;
5. a separate closure PR must be created from the protected post-bootstrap `main`;
6. that closure PR must be judged by the trusted verifier sourced from protected `main`, not by the candidate copy;
7. only after the closure PR and exact evidence bindings succeed may CS016E be appended to the acceptance chain and marked `accepted`.

## Future root replacement

Single-maintainer mode does not give the maintainer authority to replace the trusted root with a candidate root that approves itself.

If a future defect requires replacement of the root-critical workflow/verifiers, that operation remains `BLOCKED` unless an external independent authority becomes available. This is an intentional fail-closed safety boundary.

## Evidence before this decision

Run `31633164447` on head `7803ac6cd4e85f835d36841c53042c7777f5513c` completed successfully, including live GitHub provenance, D-Lab v1.5, evolution verification, product contract/assurance, manifest verification and bootstrap repository-protection verification.

At that moment, the classic branch protection still required one human approval. Because no independent reviewer exists, that configuration is not usable for the declared long-term maintainer topology. The requirement is therefore being corrected transparently before CS016E acceptance, not bypassed after acceptance.

## Product boundary

This decision changes governance only. It does not modify runtime, ABI, product tests, product claims or release authorization.

## Publication integrity note

During publication of this decision, the manifest-reconciliation trigger was emitted before the complete single-maintainer tree had been attached to the branch. The intermediate commit `b3427811e8fe889186ff2cbf5915d04eb48e0684` and its bot reconciliation `69d34d93c5762fa77e61cbeb0586064ae1e2e8e3` are therefore preserved as **non-qualifying intermediate states**.

The complete single-maintainer hardening package was then applied by fast-forward commit `e20100ff25c02c03f544482ac2b85ab6fc1c8d4a`. Neither intermediate commit is acceptance evidence, and no failure or ordering mistake is reclassified as PASS.
