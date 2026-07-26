# Hardware qualification boundary

The qualification harness is tooling around the NeoEng D-Core. It reads binaries, test output, benchmarks and environment declarations. It has no authority over canonical state, simulation ordering, rollback decisions or evidence-chain contents.

```text
D-Core canonical runtime
        |
        +-- deterministic probes and raw benchmark data
        v
qualification campaign runner
        |
        +-- hashes, environment record, decision inputs
        v
independent campaign verifier
```

Wall-clock measurements never enter the deterministic transition function. A qualification result cannot alter runtime behavior.

The result `passed` means only that the D-Core profile contract represented by `neoeng.dcore.hardware-qualification.v2` passed on the exact recorded native environment. It does not imply that another machine, driver, architecture, renderer or full-product workload passed.

Profile constraints define the named comparison target; they are not global runtime requirements. Host-local validation on any other machine is useful when the exact environment and limitations are retained. Its measured timings belong to that machine only. Nominally weaker or stronger hardware may produce better or worse results, so a new campaign is required before making a claim about another environment.
