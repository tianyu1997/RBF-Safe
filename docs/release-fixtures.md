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
   the expected proposal, records a stable operational assessment, explicitly
   activates its lifecycle, and repeats selection through the expected-head
   guarded gate;
8. registers, directly reuses, and audits an identity-bound safety-memory
   artifact;
9. authenticates fixed payload bytes against that artifact and an external
   test key;
10. publishes a second exact-byte artifact through a request/receipt-bound
    authenticated remote exchange and appends its verified transfer to a
    journal;
11. verifies a second publication offline using a caller-supplied Ed25519
    trust bundle and appends exact public key/bundle provenance;
12. grants explicit rotation permission, signs an exact trust successor,
    publishes it against an expected head, and replays the local history;
13. validates a source-bound single-robot fleet reservation, publishes its
    schedule archive, and replays the stored report; and
14. advances the scene version, then checks conservative certificate inheritance
   and retained endpoint coverage.

The executable fails on any false-safe point check, identity mismatch,
uncertified path/action, lost coverage, update failure, or missing inheritance.
Its `logical_digest` covers canonical fixture identities, discrete counts,
runtime-shield, learning-policy feedback, calibration and lifecycle,
deterministic
safety-memory identity/reuse, artifact authentication, HMAC and Ed25519 remote
transfer/journal, signed trust rotation/history,
fleet coordination and archive replay, update, and inheritance outcomes while
excluding wall-clock time, approximate memory, and
floating-point-derived identities, including certificate, calibration,
memory, attestation, fleet-version, drift-report, and lifecycle-event IDs.
Their dedicated fixed-format tests verify identity stability independently.
CI compares the logical outcomes with the committed
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

`data/policy_calibration_lifecycle_schema1` is the fixed RBF-Safe 3.5
lifecycle fixture for that profile. It contains one stable synthetic
operational window followed by explicit activation. Tests verify the fixed
report and lifecycle-head IDs, parent links, derived drift metrics, transition
replay, load budgets, exact profile binding, guarded gating, and
C++/Python/native inspection. Its active state is interoperability data, not
deployment approval or execution evidence.

`data/artifact_transfer_journal_schema1` is the fixed RBF-Safe 3.6 transfer
journal fixture. It contains one authenticated synthetic publication bound to
an exact 24-byte payload, whole-memory identity, request/receipt IDs, service
sequence, and transfer-attestation ID. Tests verify its fixed journal/transfer
IDs, checksum, parent chain, load budgets, C++/Python/native inspection, and
cross-platform schema-1 loading. It contains no key, authentication tag, or
deployable safety evidence.

`data/service_trust_bundle_schema1` is the fixed RBF-Safe 3.7 public bundle
fixture. It contains one active Ed25519 service public key and no private
material. Tests verify exact bundle/key IDs, resource caps, schema rejection,
and C++/Python/native inspection. The self-consistent fixture is
interoperability data, not an authorized production trust root.

`data/artifact_transfer_journal_schema2` is the fixed RBF-Safe 3.7 public-key
transfer fixture. It contains one Ed25519-verified publication and records its
verification-key and trust-bundle IDs. It contains no payload, signature, or
private key; standalone journal loading verifies local integrity rather than
replaying remote authentication.

`data/service_trust_bundle_schema2` is the fixed RBF-Safe 3.8
rotation-capable root bundle. `data/service_trust_history_schema1` contains
that root plus one exact signed successor and two immutable rotation records.
Tests verify bundle, authorization, record, root/head, filename, replay,
resource, corruption, and caller-anchor behavior across platforms. Both
fixtures contain public keys/signatures only and are interoperability data,
not authorized deployment trust.

`data/service_trust_bundle_schema3` is the fixed RBF-Safe 3.9 two-signature,
distinct-service root. `data/service_trust_history_schema2` contains its
canonical two-signature successor rotation, and
`data/service_trust_checkpoint_schema1` binds the exact root, head, sequence,
and record with the current quorum. Tests verify exact bundle,
authorization-set, record, checkpoint, root/head, canonical ordering,
resource, tamper, replay, and caller-anchor behavior across C++ and Python.
The files contain no private key and are not deployment trust anchors.

`data/reviewed_deployment_profile_schema1` is the fixed RBF-Safe 3.10
two-reviewer deployment fixture. It contains one schema-3 public trust root,
one root record, a governance-signed checkpoint, and a profile approved by
distinct Safety and Controls services. The exact checkpoint, profile, and
approval-set IDs are respectively
`e64197b785ba1ae0c9f349adf5c26ac114c250088504950cc8073e25a6550d32`,
`c652aa75ca153ef429b6fff372b83c675a0ac3b68f055e9bb607108d543c7be4`,
and
`e56a57547437938c39ca903541cf7eb014d921a3254fc06e8a45c96e6d8cd9ae`.
It contains public keys and signatures only, is synthetic interoperability
data, and does not authorize any deployment or execution.

`data/bounded_execution_session_schema1` is the fixed RBF-Safe 3.11
end-to-end execution-session fixture. It contains a schema-2 Atlas, schema-3
public trust root/history, signed checkpoint, two-reviewer profile, certified
three-command sequence, controller acknowledgement, and independently signed
armed monitor observation. The exact Atlas, checkpoint, profile, and session
IDs are respectively
`900f017e78acb91948f908edd9fbec5567280e4da4df8284f307b19e46fac862`,
`3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01`,
`7981b3bbd373255d5f5bd0bcfac2139ac8c55a8d841dedc15e32bbb53bf310d2`,
and
`62647c557ba9dad576c9ce3035ffe496fe0c224f91432d5b290586c09e6be2df`.
It contains no private key. The session is synthetic `Unknown` evidence, not
deployment authority; tests obtain `RuntimeExecutable` only for the fixture's
exact second command inside its stored closed window.

`data/execution_ledger_schema1` is the fixed RBF-Safe 3.12 completed ledger
for that exact bounded-session fixture. Its ledger ID and final record ID are
respectively
`f19c91b8f7788471691ac4d6a09861ee08188c9993e21861ad13e25e9cf99aa5`
and
`ef269da4406e26a5aa7621af1ca3095392fe1ca84b2327b86804024ee2a0437b`.
The seven immutable records contain one root, three exact command
authorizations with signed trust checkpoints, and three controller-signed
successful completions. Tests replay the exact session, Atlas, profile,
history, checkpoints, authorizations, completion signatures, parent chain,
resource limits, C++/Python/native inspection, and cross-platform schema-1
loading. It contains no private key, trustworthy-clock claim, controller
tracking evidence, or deployment authority. The ledger and audit are
`Unknown`; only each historical exact authorization was
`RuntimeExecutable` inside its closed window.

The reviewed RBF-Safe 3.5 cross-platform logical digest is
`7fd992c40260981c`.

The reviewed RBF-Safe 3.6 cross-platform logical digest is
`88881d57663cfeaa`. It additionally covers an exact-byte, request-bound
authenticated publication and a valid one-record transfer journal using only
discrete cross-platform fields.

The reviewed RBF-Safe 3.7 cross-platform logical digest is
`b1c149647d6e2454`. It additionally covered one
caller-pinned Ed25519 publication verification per case, exact public
key/bundle provenance, and a valid two-record schema-2 transfer journal. As in
earlier releases, timings, approximate memory, and transitive floating-point
identities are excluded.

The reviewed RBF-Safe 3.8 cross-platform logical digest is
`405c70e97f412d5c`. It additionally covered one signed
successor authorization and a valid two-record trust-history replay per case,
using only discrete cross-platform fields.

The reviewed RBF-Safe 3.9 cross-platform logical digest is
`5b830fba7deebcf8`. It additionally covered a two-signature distinct-service
rotation, schema-2 trust-history replay, and a quorum-signed checkpoint per
case using only discrete cross-platform fields.

The reviewed RBF-Safe 3.10 cross-platform logical digest is
`39214f84cf8d2d36`, recorded in
`data/release-fixtures/logical_digest.txt`. It additionally covers one profile
bound to the exact trust root, checkpoint, and head; two distinct-service
Safety/Controls approvals; and one conformant but non-executable runtime
assessment per case. Only deterministic identities and discrete counts enter
the digest.

The reviewed RBF-Safe 3.11 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `ece841463f4b76e6`. It
additionally covers a complete bounded execution session and one exact
closed-window command authorization per case. Sessions remain non-authorizing
`Unknown` evidence.

The reviewed RBF-Safe 3.12 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `50d8f3d1d579721a`. It
additionally covers one completed expected-head execution ledger per case,
including two ordered exact authorizations, two controller-signed completions,
and two current-checkpoint revalidations. Only deterministic discrete counts
enter the digest; ledger/audit state remains non-authorizing `Unknown`
evidence.
