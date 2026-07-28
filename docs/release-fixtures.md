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

After all robot cases, the benchmark replays the fixed schema-1 provenance
bundle once against its exact trust root and signed checkpoint at evaluation
time `1000000`. The deterministic digest binds the resulting bundle,
hardware-report and freshness-report IDs plus their discrete quorum counts.

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

`data/transparency_log_schema1` is the fixed RBF-Safe 3.13 two-record
transparency log. Its caller-pinned identity is:

- namespace: `rbfsafe-public-deployments-v1`;
- log ID:
  `e77f9b5d98d731c0b2e6f41486c3c6870488962aa77d5c12fca4eb5e160655d4`;
- signer service: `transparency-log`;
- signer key ID:
  `02348249fded6a7cf712333d50a6318aaa1309056318af5c049e4ce296cf10e8`;
- signer public key:
  `6c8d14b047593d7117dc445957d8bdd6772c303d064297fdc8e329927d370bda`;
- newest checkpoint:
  `86d47335bee5850b9c3a404e123d50bdb751cabee03a76de6361b8e25f03772f`;
- newest Merkle root:
  `fe8e39f32feae84fae08a914375b1e3fec1afff94f57f97ff7955b3945c14eb1`.

Record zero publishes deployment-anchor leaf
`008ccec227af2ccb96831e28e478395e952f6f1fde6f340d63d63a7aaae9874a`;
record one publishes two-source runtime-observation leaf
`00b194c093fb1f84c58a7f220b5a5e3aaafc77199baf094b6319811fb69e0bbb`.
The fixture contains public signatures but no private key. Tests verify both
record/checkpoint chains, inclusion proofs, prefix consistency, bounded
cross-platform replay, C++/Python/native inspection, and exact caller pins.
All values remain non-authorizing `Unknown`.

`data/transparency_gossip_archive_schema1` is the fixed RBF-Safe 3.14
two-record witnessed-checkpoint archive. It reuses the v3.13 log identity and
the bounded-session fixture trust bundle. Its exact caller pins are:

- log ID:
  `e77f9b5d98d731c0b2e6f41486c3c6870488962aa77d5c12fca4eb5e160655d4`;
- trust bundle:
  `3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780`;
- archive head:
  `fd5ac959b484ada7ea2ce15e7cc8bccf41d8b6eaa368d9dafc2aedcdb0036514`.

Each checkpoint has two distinct-service cosignatures. Record one adds compact
proof `a42b62c19bb0c9d7ac25db098f7465fb4fc49d9e12802d96dfd25ffa62880201`
from tree size one to two. C++ and Python tests replay both signatures, the
global and sender chains, proof relation, resource bounds, expected head, and
native/Python inspection. It contains no private keys and makes no physical
observation, trustworthy-time, delivery, or execution claim.

`data/provenance_bundle_schema1` is the fixed RBF-Safe 3.15 two-attester,
two-time-source provenance bundle. Its exact caller pins are:

- provenance bundle:
  `eef49057478c4545ade19a52aee0965539956235bd46dbd480c54da77ede9690`;
- trust root/bundle:
  `af93de1f517b91d348732b72bd08becb9411ee08c1151f5f66da0291740a2865`;
- trust checkpoint:
  `6aa7aa5c91644205fa67e0caf6858a7f11ba0bb03aec2c548ba941624984137b`;
- evaluation time: `1000000` in clock namespace `unix-utc-ns`.

The hardware chain pins one synthetic TPM2 normalizer and vendor, requires
artifact-publication and execution-control scopes, and contains two distinct
attester signatures. The two source intervals intersect and produce `FRESH`
at the fixed evaluation value. The fixture contains public keys, signatures,
and synthetic digests only: it has no private key, raw vendor evidence,
trusted hardware, trusted clock, or execution authority. All reports remain
`Unknown`.

`data/continuous_fleet_occupancy_schema1` is the fixed RBF-Safe 4.0
two-deployment swept-link occupancy bundle. Both deployments use the included
synthetic planar 2R robot under one explicit logical timeline and workspace
frame, with fixed translations that conservatively separate all eight stored
slices. Its exact identities are:

- occupancy bundle:
  `d9a6a28c80ae86a28b996c8da954c33c725d9883a22f9f080f22d51e72be4231`;
- fleet report:
  `05fc3206ce76135946763fca75e3a399449a80b44a47ae168d564a439aa280ef`.

`data/continuous_fleet_occupancy_schema2` is the fixed RBF-Safe 4.1
bounded-frame fixture for the same synthetic planar 2R robot. Both
deployments use an exact 90-degree right-handed base rotation and explicit
translation uncertainty `[0.01, 0.01, 0.02]`; all eight stored slices remain
separated. Its exact identities are:

- occupancy bundle:
  `6030e3574db5634f60b6cf04ffc325077f944ef23d256ef7cc937fe857dce8d0`;
- fleet report:
  `9a8b12df9ba0ca88142a9f44ef722a411cb33930a8986e4dd7b657d8d333053e`;
- serialized file SHA-256:
  `d5c566782ab97847dcef150b81cdaeb73c27192bfd8c12a7e34ea0787d010c2f`.

C++ and Python tests load the same bytes, replay both occupancies from the
included robot model, replay the fleet report, and verify native/Python
inspection. The `CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES` status remains
non-authorizing `Unknown`; the fixture has no obstacles, physical clock,
tracking, controller, or hardware claim.

`data/occupancy_publication_schema1` is the fixed RBF-Safe 4.2 authenticated
publication for the exact schema-2 occupancy bytes above. It uses one active
publication-only Ed25519 service key, a root stream statement valid for ticks
0 through 32, and no private key material. Its stable identities are:

- publication:
  `90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251`;
- trust bundle:
  `89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d`;
- publisher key:
  `db69491130c011ccd19cb5d0b86259e6cc627840f0dd83f5b55ce7a37ee494dd`;
  and
- verification at tick 16:
  `9910e71348c6b69609bb2026e7a7f926d27bca243bc2140a0259b7ece9d8fe09`.

C++ and Python tests load the same publication and trust bytes, verify the
same external occupancy file under exact caller pins, and run both
inspectors. The fixture demonstrates authenticated software provenance only;
it has no network, consensus, trusted time, collision, tracking, hardware, or
execution claim.

`data/occupancy_publication_history_schema1` is the fixed RBF-Safe 4.3
two-record history. It retains the exact public trust snapshot, two signed
publication files, and two exact occupancy payload files. Its stable
identities are:

- root publication:
  `90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251`;
- head publication:
  `83a6952083ac661aacff43168473c1938e29adfe738275d2230458dd6074dfb9`;
- root record:
  `d61cfb66d7473dd731559758e6f6b29327f9e326d3346817dd05c9db95b4cedb`;
- head record:
  `34fad28d5893818a1cfd79e3195e9ff757fff1764fed10fae326e7f4ef12fcf9`;
- trust bundle:
  `89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d`;
  and
- manifest identity:
  `c84abd70869f00209234a7e84263322caaab3270b6ebba0bc4db04bca9348049`.

Tests reopen the fixture under exact stream, publisher, trust, root, and
expected-head pins; replay both signatures and payloads; verify the head at a
covered tick; and exercise identical, extension, reverse-extension, and fork
audits. The fixed history contains public test material only. It does not
provide head distribution, network replication, consensus, trusted time,
collision certification, or execution authority.

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

The reviewed RBF-Safe 3.13 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `7447f9684a0c9e4a`. It
additionally publishes one exact deployment anchor and one two-source
independent runtime observation to a signed two-leaf transparency log per
case, verifies one inclusion proof and one prefix-consistency witness, and
reopens against the retained expected checkpoint. Only deterministic discrete
counts enter the digest; all transparency evidence remains non-authorizing
`Unknown`.

The reviewed RBF-Safe 3.14 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `2e41e0968d06eec0`. It
additionally verifies two compact append-only proofs, six checkpoint
cosignatures, a three-record authenticated gossip archive, one consistent
proof-graph audit, and one same-size-equivocation split-view conflict per
case. Only deterministic discrete counts and statuses enter the digest; all
witness and gossip evidence remains non-authorizing `Unknown`.

The reviewed RBF-Safe 3.15 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `ae009ce1b6a78f78`. It
additionally replays the fixed two-attester, two-time-source provenance
fixture against its exact trust root and checkpoint, and binds the bundle,
hardware report, freshness report, statement count, and source count into the
digest. The `SATISFIED + FRESH` outcome remains non-authorizing `Unknown`.

The reviewed RBF-Safe 4.1 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `156c1b1e36828399`. It loads and robot-replays both
fixed continuous fleet occupancy bundles and binds their bundle/report
identities, schemas, occupancy/slice counts, frame classification, and
separation status into the digest. Timings and memory estimates remain
diagnostic; the occupancy result remains non-authorizing `Unknown`.

The reviewed RBF-Safe 4.2 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `43b03f055ffa8777`. It
additionally authenticates the fixed occupancy publication against its exact
payload, public trust bundle, stream, publisher, root parent, and evaluation
tick, then binds only stable identities and discrete values into the digest.
Publication and verification results remain non-authorizing `Unknown`.

The reviewed RBF-Safe 4.3 cross-platform logical digest is recorded in
`data/release-fixtures/logical_digest.txt` as `5fec5ce7cd17c84f`. It
additionally replays the fixed two-record occupancy publication history,
verifies its externally pinned head at a covered tick, and audits the history
against itself. Only stable identities, the record count, and the discrete
relation enter the digest. History and audit results remain non-authorizing
`Unknown`.
