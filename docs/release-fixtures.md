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
6. gates an accepted and a low-confidence policy proposal and validates their
   aligned feedback labels;
7. applies a scoped empirical calibration profile and conservatively selects
   the expected proposal;
8. registers, directly reuses, and audits an identity-bound safety-memory
   artifact;
9. authenticates fixed payload bytes against that artifact and an external
   test key;
10. validates a source-bound single-robot fleet reservation, publishes its
    schedule archive, and replays the stored report; and
11. advances the scene version, then checks conservative certificate inheritance
   and retained endpoint coverage.

The executable fails on any false-safe point check, identity mismatch,
uncertified path/action, lost coverage, update failure, or missing inheritance.
Its `logical_digest` covers canonical fixture identities, discrete counts,
runtime-shield, learning-policy feedback and calibration, deterministic
safety-memory identity/reuse, artifact authentication, fleet coordination and
archive replay, update, and inheritance outcomes while
excluding wall-clock time, approximate memory, and
floating-point-derived certificate IDs. CI compares it with the committed
expected digest on every platform instead of applying a machine-dependent
timing threshold.

The 128-iteration test is a quick integration gate. The 8192-iteration test is
labelled `soak` and remains bounded by CTest timeout and benchmark resource
limits. Use larger counts for local profiling only; they do not strengthen a
geometric certificate.

## Fixed safety-memory fixture

`data/safety_memory_schema1` is the fixed RBF-Safe 3.0 memory fixture. It
contains two active Atlas catalog entries and one audited cross-task reuse.
The C++ memory test verifies its payload checksum, deterministic artifact and
event IDs, sequence replay, state/generation summary, and a fixed first
artifact ID on every supported platform. It is synthetic interoperability
data, not physical-robot calibration or deployment certification.

`data/safety_memory_store_schema1` is the fixed RBF-Safe 3.1 store fixture. It
contains a one-artifact active root and a second revision that marks the
artifact stale. Tests verify the root/current revision IDs, parent chain,
memory identities, commit filenames, schema-1 payloads, and historical reads.

`data/fleet_schedule_archive_schema1` is the fixed RBF-Safe 3.2 fleet archive
fixture. It contains a conflict-free root and a conflicted child for two
declared robot envelopes. Tests verify fixed version and head IDs, whole-memory
and fleet-snapshot bindings, report semantics, parent continuity, aggregate
limits, checksum failures, and cross-platform schema-1 loading.

`data/artifact_attestation_schema1` is the fixed RBF-Safe 3.3 attestation
fixture. It contains a 24-byte synthetic payload and schema-1 sidecar created
with the public test-only key bytes `01 02 ... 20`. Tests verify fixed artifact,
payload, attestation and HMAC identities, bounded loading, exact lifecycle
binding, wrong-key rejection, and C++/Python/native inspection. The key is
public interoperability data and must never protect real artifacts.

`data/policy_calibration_profile_schema1` is the fixed RBF-Safe 3.4 profile
fixture. It contains two synthetic reliability bins with 1,000 aggregate
observations. Tests verify the fixed profile ID, contiguous coverage, exact
model/scope/task/data identity, recomputed ECE and Wilson bounds, bounded
loading, C++/Python queries, and native/CLI inspection. It is interoperability
data, not evidence that any deployed policy is calibrated.
