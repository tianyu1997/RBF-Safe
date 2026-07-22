# Release fixtures and benchmark

RBF-Safe 1.0 includes deterministic integration fixtures in
`data/release-fixtures`. They exercise named robot dimensions and public input
formats without depending on RapidBoxForest, downloaded assets, ROS, OMPL, or
a previous RBF-Safe binary.

## Fixture inventory

The robot set contains IIWA14, UR5, Panda, and Franka serial DH descriptions.
The scene set contains shelf, industrial-cell, clutter, and
mobile-manipulation AABB snapshots. `cases.tsv` binds them into four start/goal
cases. `manifest.json` identifies the fixture collection and intended schema;
`logical_digest.txt` fixes the reviewed cross-platform logical result.

These are API and regression fixtures. The obstacle AABBs are intentionally
placed far from the robot so that every supported CI platform can exercise
Atlas construction, trajectory auditing, shield decisions, scene-version
updates, and certificate inheritance without making performance sensitive to
a narrow geometric boundary. Robot and scene values are not calibrated
descriptions of a physical system, realistic planning benchmarks, or evidence
for deployment.

## Release gate

Enable the optional benchmark target and run either the executable or CTest:

```bash
cmake -S . -B build -DRBFSAFE_BUILD_TESTS=ON -DRBFSAFE_BUILD_BENCHMARKS=ON
cmake --build build --config Release --target rbfsafe_release_benchmark
ctest --test-dir build -C Release -R rbfsafe_release_benchmark --output-on-failure
```

For each case the benchmark:

1. loads the public robot and scene JSON formats;
2. builds and identity-checks an Atlas from the registered endpoints;
3. continuously audits the start-to-goal path;
4. performs bounded repeated membership and independent point-collision checks;
5. verifies an accepted runtime-shield action; and
6. advances the scene version, then checks conservative certificate inheritance
   and retained endpoint coverage.

The executable fails on any false-safe point check, identity mismatch,
uncertified path/action, lost coverage, update failure, or missing inheritance.
Its `logical_digest` covers canonical fixture identities, discrete counts, and
required outcomes while excluding wall-clock time, approximate memory, and
floating-point-derived certificate IDs. CI compares it with the committed
expected digest on every platform instead of applying a machine-dependent
timing threshold.

The 128-iteration test is a quick integration gate. The 8192-iteration test is
labelled `soak` and remains bounded by CTest timeout and benchmark resource
limits. Use larger counts for local profiling only; they do not strengthen a
geometric certificate.
