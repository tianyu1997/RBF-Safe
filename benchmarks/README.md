# Release benchmark and soak gate

`rbfsafe-release-benchmark` consumes only installed public RBF-Safe APIs and
the checked-in synthetic fixtures under `data/release-fixtures`. It builds and
updates one Atlas per case, audits a continuous path, checks a runtime-shield
action, estimates public Atlas memory and measures wall time, and independently point-checks
every certified query. Any false-safe sample, identity mismatch, lost
coverage, failed inheritance, or non-certified action fails the process.

Timing values are reported but deliberately have no hard CI threshold: shared
runner timing is not reproducible. The release gate instead bounds iteration
counts and requires deterministic logical results. `logical_digest` excludes
timings, approximate memory, and floating-point-derived certificate IDs. It
encodes canonical fixture identities, discrete counts, certification outcomes,
shield acceptance, and update compatibility, then must match the committed
`logical_digest.txt` on every platform.

```bash
cmake -S . -B build -DRBFSAFE_BUILD_BENCHMARKS=ON
cmake --build build --target rbfsafe_release_benchmark
./build/rbfsafe-release-benchmark \
  --fixtures data/release-fixtures --iterations 1000 --json
```

CTest registers a 128-iteration smoke gate and an 8192-iteration soak gate.
The fixtures are synthetic regression inputs, not calibrated robot, workcell,
or collision models and must never be used as deployment evidence.
