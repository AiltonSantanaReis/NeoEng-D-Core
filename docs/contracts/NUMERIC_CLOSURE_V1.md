# Numeric closure contract v1

Schema: `neoeng.dcore.numeric-closure.v1`

Baseline: 1.11.0

Requirements: `DCORE-NUM-001`, `DCORE-NUM-002`, `DCORE-NUM-003`

## 1. Normative decisions

The CS011 closure deliberately narrows unsupported promises instead of treating
laboratory results as production certificates.

1. The Y1-O4 "Lyapunov proxy <= 0.3 ms and correlated" proposal is rejected as
   a runtime product claim. The historical uncertainty and RAA laboratories
   measure enclosures, contact ambiguity and empirical containment; they do not
   implement a named Lyapunov estimator or demonstrate predictive correlation
   with authoritative runtime failures.
2. A global certificate for the full composed numerical pipeline is rejected.
   No result may be described as proving all operation sequences or the full
   `int64_t` state space.
3. The accepted certificate surfaces are the Q32.32 primitive contract and the
   explicitly scoped solver certificates below.

The laboratory code remains available for research and shadow observation. It
does not alter authoritative state and does not create a runtime Y1-O4 claim.

## 2. Q32.32 primitive domain

`Fixed::rep` is signed `int64_t`, with scale `2^32`. Every primitive accepts raw
operands from the full signed 64-bit storage domain.

| Operation | Exact intermediate | Acceptance |
|---|---|---|
| add/subtract/negate | signed 128-bit integer | result fits `int64_t` |
| multiply | `(lhs_raw * rhs_raw) / 2^32` | truncated toward zero and fits `int64_t` |
| divide | `(lhs_raw * 2^32) / rhs_raw` | divisor nonzero, truncated toward zero and fits `int64_t` |
| integer/ratio construction | signed 128-bit integer | divisor nonzero and result fits `int64_t` |

A result outside `int64_t` raises `std::overflow_error`. Division by zero raises
`std::domain_error`. No saturation, wraparound or partially updated value is
accepted. Signed 128-bit is wide enough for every product of two signed 64-bit
raw operands and for every raw-times-scale intermediate.

This is a primitive certificate, not a proof that an arbitrary composed
workload will stay inside the result domain. Callers must handle rejection or
constrain their workload.

## 3. RAA status

The fixed-capacity RAA path remains a laboratory and shadow observer:

- logical capacity is 2 through 16 retained terms;
- narrowing, wide addition and error-ID exhaustion reject before wraparound;
- discarded terms and local truncation guards accumulate in the residual;
- empirical Monte Carlo and interval-audit results apply only to the recorded
  corpus;
- no global containment, ARM64 or authoritative integration claim is allowed.

RAA evidence may support research decisions. It cannot be cited as the rejected
Y1-O4 runtime proxy or as a global numerical certificate.

## 4. Oblique solver scopes

Solver evidence is classified before any certification claim is made.

### 4.1 Exact continuous rational oracle

`solve_exact_oblique_tree_active_sets` is accepted as an exact continuous
certificate only when all of these conditions hold:

- input validation passed;
- the contact graph is a tree;
- `1 <= body_count <= 10`;
- `contact_count <= 9` and `contact_count + 1 == body_count`;
- the rational active-set certificate passed.

The proof covers the rational encoding of the supplied Q32.32 velocities,
integer masses and Q1.30 normals. Its rounded vectors are not themselves the
proof.

### 4.2 Finite-grid oracle

`solve_oblique_tree_grid_dp` is exact only on its configured finite Cartesian
grid. `certified_on_grid` must never be presented as a continuous Q32.32
certificate.

### 4.3 Residual certificates

A runtime solver result is certified only when its documented residual
predicate reports `certified == true`. The certificate is limited to those
residuals and tolerances; it is not a proof of global optimality unless the
specific solver contract says so.

### 4.4 Connected coordinate fallback

The connected arbitrary-normal coordinate path is deterministic and
operational, but explicitly non-certified. A structured tree candidate is used
only when its own certificate passes; otherwise the runtime uses the canonical
connected fallback. An operational fallback result may not be relabeled as an
exact, finite-grid or residual certificate.

Invalid input, capacity failure and numerical overflow are rejected
fail-closed.

## 5. Allowed public language

Allowed:

> NeoEng D-Core provides checked Q32.32 primitives and explicitly scoped
> numerical certificates. Unrepresentable primitive results are rejected.

> The small-tree rational oracle is exact in its declared input and size scope.

> Connected oblique fallback is deterministic and operational, not globally
> certified.

Prohibited:

> The full numerical pipeline is globally certified for containment and
> overflow.

> Y1-O4 provides a production Lyapunov predictor under 0.3 ms.

> Finite-grid or empirical RAA evidence proves all real inputs.

## 6. Evidence

Closure requires:

- boundary verifier and fail-closed mutation self-test;
- primitive extreme-value and adversarial tests;
- RAA capacity and guarded-operation tests;
- exact-small-tree and solver-classification tests;
- deterministic long probe;
- GCC/Clang comparison;
- immutable campaign manifest and independent verification.

ARM64 and external mathematical audit remain unexecuted and cannot be inferred.
