# Versioning and compatibility

## Library versions

RBF-Safe uses Semantic Versioning. Version 1.0 established the public source
compatibility promise. Version 2.0 retained it and added learning-policy
safety. Version 3.0 retains the complete documented 2.0 surface and adds the
persistent safety-memory and fleet-coordination module. Version 3.1 adds an
immutable optimistic-concurrency memory revision store. Version 3.2 adds a
deterministic versioned fleet-schedule archive. Version 3.3 adds symmetric
artifact attestations and the `RBFSafe::trust` target. Version 3.4 adds
identity-bound policy-calibration profiles and conservative calibrated gating
to `RBFSafe::policy`. Version 3.5 adds operational drift reports, a
parent-linked calibration lifecycle, and expected-head guarded gating. Version
3.6 adds the transport-neutral `RBFSafe::remote` contract, request-bound
service authentication, and transfer journals. Version 3.7 adds
`RBFSafe::identity`, caller-pinned Ed25519 service trust, monotonic key
rotation, and offline transfer verification. Version 3.8 adds explicit
rotation authority, exact-successor signatures, schema-2 trust bundles, and
expected-head guarded replayable trust histories. Version 3.9 adds immutable
quorum/distinct-service rotation policy, schema-3 trust bundles, schema-2
authorization-set histories, and caller-pinned signed trust checkpoints.
Version 3.10 adds reviewed deployment profiles and role-aware signed approval
quorums under exact trust checkpoints. Version 3.11 adds bounded execution
sessions, explicit controller/monitor acknowledgements, and exact
closed-window command authorization. Version 3.12 adds an append-only
expected-head execution ledger, current-checkpoint reviewer revalidation,
signed controller completions, terminal event records, and offline audit.
Version 3.13 adds exact deployment anchors, independently signed runtime
observations, a deterministic Merkle transparency log, signed checkpoints,
inclusion proofs, explicit prefix-consistency witnesses, and bounded
expected-head persistence. Version 3.14 adds compact frontier/subtree
consistency proofs, independently cosigned checkpoint quorums, authenticated
gossip, split-view detection, and bounded append-only gossip archives.
Version 3.15 adds explicitly scoped adapter-normalized hardware-key statement
chains, signed external-time source chains, caller-pinned freshness policy,
combined replay, and bounded provenance-bundle persistence.
Version 4.0 retains the complete documented 3.x surface and adds
`RBFSafe::occupancy`, conservative piecewise-linear swept-link occupancy,
bounded fleet separation analysis, replay verification, and an independent
bundle format.
Version 4.1 additively adds bounded deployment frames with nominal
right-handed rotations, axis-wise translation uncertainty, arbitrary-axis
angular uncertainty, and occupancy-bundle schema 2 while retaining the
translation-only API and schema-1 reader.
Version 4.2 additively adds `RBFSafe::coordination`, exact-byte Ed25519
occupancy publication, caller-pinned monotonic stream verification, and an
independent schema-1 publication format while retaining every earlier
occupancy API and format.
Version 4.3 additively adds caller-pinned immutable occupancy publication
histories, exact stored-payload replay, expected-head append, deterministic
prefix/fork audit, and an independent schema-1 history directory while
retaining every 4.2 publication file and API.
Version 4.4 additively adds deterministic moving workspace-AABB occupancy,
bounded robot-scene swept-envelope analysis and conflict witnesses, exact
moving-obstacle replay, and an independent schema-1 robot-scene bundle while
retaining every 4.3 history, publication, and fleet-occupancy API and format.
Version 4.5 additively adds an independent trust-rotating occupancy history,
single/quorum trust successor commits, historical-bundle replay,
trust/publication dual-chain audit, expected-head and signed-checkpoint trust
anchors, and schema-1 persistence while retaining the fixed-trust 4.3 history
unchanged.
Version 4.6 additively adds unanimous coordinated reservation participants and
agreements, exact multi-history payload/prefix replay, parent-linked round
succession, and independent schema-1 persistence while retaining every 4.5
history and format unchanged.
Version 4.7 additively adds a standard-library `GeometricJacobian`, analytic
modified-DH end-effector Jacobian evaluation, the corresponding Python API,
and a checked original-project scope traceability manifest. It changes no
input or storage schema and retains every 4.6 declaration and format.
Version 5.0 consolidates the same public declarations into module-level C++
headers and removes the former fine-grained include paths. Storage schemas,
evidence meanings, Python names, and installed CMake target names are
unchanged. Documented module headers, installed CMake target names, and
high-level Python names remain source compatible throughout the 5.x line.
Additive API changes may appear in minor releases; deprecated 5.x APIs may be
removed only in 6.0. The exact policy and automated review gate are documented in
[API stability](api-stability.md).

C++ ABI compatibility is not promised across compilers, standard libraries,
runtime-library selections, build modes, or RBF-Safe releases. Downstream C++
applications should rebuild after an update.

The Python package follows the same version as the C++ project. Supported
Python versions and wheel platforms are release metadata, not permanent API
guarantees.

## Input and storage schemas

Robot JSON, scene JSON, standalone LECT storage, and Atlas storage carry
explicit schema numbers. These schemas are independent from the library
version. A library release must not silently reinterpret an existing schema.

The v0.6 Atlas reader accepts schemas 1 and 2. New builds and dynamic updates
write schema 2; a loaded schema-1 Atlas is preserved when saved without an
update. Unknown schemas return `IncompatibleFormat`; malformed data returns
`CorruptData` or `ResourceLimit`. Legacy RapidBoxForest caches are never
interpreted as RBF-Safe data.

The v0.2 library added trajectory-audit APIs without changing Atlas schema 1,
LECT schema 1, or the robot and scene JSON schemas. A v0.1 Atlas remains
loadable when its schema and checksums are valid.

The v0.3 optional OMPL component also leaves all storage and input schemas
unchanged. It is requested as a CMake component and is not part of the core
target or Python wheel ABI. The in-memory region query BVH is reconstructed
from schema-1 regions and is not persisted.

The v0.4 corridor directory is a separate schema-1 format with a checksummed
JSON payload. It does not change Atlas schema 1 or LECT schema 1. Advanced OBB,
portal, and route certificates add an in-memory `subject_digest`; existing
Atlas certificates leave it empty and retain their historical IDs and encoded
records.

Atlas schema 2 binds each regional certificate to its exact C-space AABB and
adds link-envelope dependencies, unresolved repair domains, Atlas version
identity, certificate lineage, and complete scene-transition documents. A
schema-1 Atlas lacks this evidence, so its first dynamic update revalidates
regions instead of inheriting certificates.

The Atlas version-store format has its own schema 1. Immutable Atlas versions
retain their independent schema and payload checksums; `store.json` records
the validated parent graph and active head.

The v0.7 region database has another independent schema 1. It stores complete
AABB, OBB, Portal, TrajectoryTube, zonotope, and Taylor geometry in a
checksummed JSON payload and rebuilds its graph on load. It neither changes
Atlas schema 2 nor corridor schema 1. Conversion from those formats is an
explicit in-memory import, not transparent reinterpretation.

The v0.8 planning and optimization targets do not change any input or storage
schema. Certified roadmaps, OMPL planner state, linear constraint programs,
and trajectory assignments are derived in-memory artifacts. Persisting one in
a future release requires a separately versioned format rather than reusing an
Atlas, corridor, or region-database schema.

The v0.9 runtime shield also leaves every input and storage schema unchanged.
Actions, decisions, proposal batches, telemetry snapshots, and monitor state
are transient application-facing values. Decision IDs are deterministic
audit identifiers for the exact in-memory decision content; they are not a
new persistent schema or a replacement for Atlas certificate IDs.

The v1.0 stabilization release also leaves all storage schemas unchanged. It
adds an explicit schema support/migration matrix, fixed-format regression
gates, reviewed public-source snapshot, and public-API release benchmark. See
[Schema support and migrations](schema-migrations.md).

The v2.0 learning-policy module adds an independent policy-feedback schema 1.
It stores identity-bound, aligned gate/shield feedback records and is never
interpreted as an Atlas, corridor, generalized region database, or execution
log. Existing schemas and their bytes remain unchanged. The 2.0 reader applies
record-count and payload-size limits, checksum and deterministic-ID checks,
duplicate rejection, and label/evidence consistency validation.

The v3.0 memory module adds an independent safety-memory schema 1. It catalogs
other artifacts by digest and locator but never embeds or reinterprets their
payloads. Its reader verifies bounded counts and bytes, payload checksum,
deterministic artifact/event IDs, strict sequence order, lifecycle generation,
and a complete replay of final state. Fleet snapshots and schedule reports are
deterministic in-memory coordination artifacts in 3.0; a fleet schedule may be
registered by its report digest, but has no separate persistent format and is
not an execution certificate.

The v3.1 safety-memory-store schema 1 wraps complete, unchanged memory schema-1
directories in a deterministic linear revision chain. The immutable root and
commit documents bind memory identities, parent IDs, and decimal-string
sequences. Publication uses a cross-process writer lock and a required
expected head, then atomically introduces a new commit filename. The store
does not merge histories, authenticate artifact locators, or change contained
memory evidence.

The v3.2 fleet-schedule-archive schema 1 stores a deterministic linear history
of complete fleet snapshots and schedule reports. Each version binds the
whole-memory identity used to validate source artifacts. The reader verifies
checksums, identities, report semantics, aggregate limits, sequence and parent
continuity, and the active head. This format is independent of safety-memory
and store schemas and does not promote a coordination report to a certificate.

The v3.3 artifact-attestation schema 1 is an independent JSON sidecar. Its
deterministic ID binds exact artifact lifecycle and payload metadata; its
HMAC-SHA256 tag binds those fields to a caller-managed shared key. Loading a
sidecar is not verification. Existing Atlas, memory, corridor, feedback, and
fleet formats remain byte-for-byte independent and retain their validators.

The v3.4 policy-calibration-profile schema 1 is an independent bounded JSON
record. Its ID binds canonical source observations and scope identities;
derived ECE, maximum error, empirical rates, and Wilson bounds are recomputed
on load. It does not modify policy-feedback schema 1 or authenticate its own
author.

The v3.5 policy-calibration-lifecycle schema 1 is a separate bounded JSON
record tied to one exact profile ID. Its reports bind source aggregates and
thresholds; derived metrics are recomputed on load. Its events form a
deterministic parent chain with explicit reviewed transitions, and loading
replays the complete state machine. It does not alter the profile or feedback
schemas, authenticate observations or reviewers, or raise evidence.

The v3.6 artifact-transfer-journal schema 1 is an independent bounded
directory. It stores an append-only identity chain of compact verified
transfer metadata and omits payloads, authentication tags, and keys. The
remote verification operation binds exact current memory/lifecycle, request,
response, service, byte, and HMAC identities before append. Loading the
journal checks integrity and history, not remote authenticity.

The v3.7 artifact-transfer-journal writer emits schema 2. It retains every
schema-1 field and adds public `verification_key_id` and `trust_bundle_id`
provenance for Ed25519-verified transfers. The reader accepts schemas 1 and 2.
Empty provenance is excluded from existing HMAC and unauthenticated transfer
identity input, so 3.6 IDs remain stable.

The v3.7 service-trust-bundle schema 1 is a bounded JSON document containing
only public Ed25519 keys, state, operation permissions, inclusive
service-sequence windows, and a deterministic sequence/parent identity.
Private keys are never stored. Bundle identity verifies integrity but does not
authorize a root; the caller must pin or otherwise approve it out of band.

The v3.8 service-trust-bundle writer emits schema 2 for new roots and
successors. Plain save of a loaded schema-1 object preserves schema 1 and its
historical ID. Schema 2 adds an immutable `allow_rotate` permission to each
complete key-policy record and bundle identity. The reader accepts schemas 1
and 2; schema-1 keys load with rotation disabled and cannot silently authorize
a successor or initialize a signed history.

The v3.8 service-trust-history schema 1 is an independent immutable directory
of complete schema-2 bundles and signed rotation records. Replay verifies the
caller-pinned root, strict record/bundle parent chains, every monotonic key
transition, and every exact-successor Ed25519 signature under bounded resource
and cancellation limits. Publication uses expected-head optimistic
concurrency and cross-process exclusion. A caller-retained expected head is
required to detect restoration of the entire directory to an older
self-consistent state.

The v3.9 service-trust-bundle schema 3 adds an immutable
`minimum_signatures` and `require_distinct_services` rotation policy. New
schema-3 roots are created explicitly; `create` retains schema 2 and every
successor preserves the predecessor schema. The reader accepts schemas 1, 2,
and 3. No operation upgrades a historical bundle or chain silently.

The v3.9 service-trust-history schema 2 stores complete schema-3 bundles and a
canonical authorization set for each successor. Replay verifies every unique
Ed25519 signature plus the bundle's signature/service quorum under an explicit
per-rotation resource bound. Schema-1 histories remain readable and retain
their single-authorization record identities.

The v3.9 service-trust-checkpoint schema 1 is an independent bounded JSON
statement binding an exact root bundle, head bundle, head sequence, and head
record to a canonical set of current-head signatures. Verification requires
caller-provided root and checkpoint IDs and a full history replay. The format
does not discover roots, select the newest checkpoint, or prove wall-clock
freshness.

The v3.10 reviewed-deployment-profile schema 1 is an independent bounded JSON
document containing one deterministic deployment profile and canonical
role-aware Ed25519 approval set. It binds exact robot/controller/platform/
runtime and trust root/checkpoint/head identities plus immutable runtime
constraints and review policy. Loading replays the caller-pinned history and
verifies every approval. It does not reinterpret any prior schema, attest
hardware/runtime observations, or raise evidence above `Unknown`.

The v3.11 bounded-execution-session schema 1 is an independent bounded JSON
document containing one exact certified command sequence, reviewed-profile
and trust bindings, role-aware execution approvals, controller
acknowledgement, and monitor-signed runtime observation. Loading recomputes all
identities, replays the trust/profile/Atlas chain, and recreates session time
bounds. It does not alter any prior schema. The persisted session remains
`Unknown`; `RuntimeExecutable` exists only in an ephemeral exact-command
authorization returned for caller-supplied monotonic time.

The v3.12 execution-ledger schema 1 is an independent immutable-record
directory bound to one exact bounded execution session. The manifest and each
record carry deterministic identities; records use strict sequence/parent
links and store exact authorizations, signed controller completions,
caller-pinned trust checkpoints, or terminal cancellation/expiry/revocation
events. Expected-head mutation and bounded replay do not modify or reinterpret
the v3.11 session schema. Loaders reject unknown ledger/record schemas and no
automatic migration is defined.

The v3.13 transparency-log schema 1 is an independent immutable-record
directory. Its manifest contains one caller-pinnable namespace and Ed25519 log
identity. Each atomic record contains one deployment-anchor or runtime-
observation leaf plus the signed checkpoint for the resulting tree. Record
sequence/parent/checkpoint chains, Merkle roots, source attestations, resource
limits, and caller-provided expected checkpoint are revalidated on load. The
explicit consistency witness carries the ordered new-tree leaf IDs; no compact
proof is inferred from it and no implicit migration is defined. Version 3.14
adds a separate compact proof value without changing the schema-1 log bytes.

The v3.14 transparency-gossip-archive schema 1 is an independent immutable
record directory pinned to one exact log identity and trust bundle. Records
carry complete witnessed checkpoints, optional compact proofs, authenticated
sender/recipient sequence chains, and a global archive sequence/parent chain.
Expected-head publication, bounded replay, and proof-graph audit do not alter
the underlying transparency-log schema. Schema 1 does not rotate its pinned
trust bundle in place.

The v4.0 continuous-fleet-occupancy-bundle schema 1 is an independent
checksummed JSON file. It stores exact trajectory, robot digest,
timeline/frame/deployment metadata, fixed base translation, deterministic
IFK-AA swept-link slices, and a replayable fleet report. Loading validates
complete time coverage and replays fleet comparisons, but exact robot models
remain external and must be supplied to
`verify_robot_trajectory_occupancy`.

The v4.1 continuous-fleet-occupancy-bundle schema 2 adds the exact row-major
nominal rotation, translation-uncertainty half-widths, and angular uncertainty
bound to each current occupancy. Those values participate in slice and
occupancy identities and robot-model replay. A schema-2 bundle may
deliberately contain schema-1 occupancies; a schema-1 bundle cannot contain
schema-2 values. No loader infers frame uncertainty for a schema-1 record.

The v4.2 continuous-fleet-occupancy-publication schema 1 is an independent
checksummed JSON file. It binds exact occupancy payload bytes and decoded
bundle/timeline/frame identities to one publisher stream, trust bundle,
sequence/parent, and closed tick window under Ed25519. Loading is structural;
cryptographic and payload verification requires explicit caller pins and the
external exact payload. No unsigned bundle is upgraded, and no current parent,
trust head, or evaluation tick is inferred.

The v4.3 occupancy-publication-history schema 1 is an independent directory
format. Its manifest fixes one stream, publisher, trust bundle, root,
timeline, and frame. Immutable records select exact publication files and
occupancy payload bytes; loading replays every signature, byte binding, and
parent. The head is reconstructed and must match a caller-retained pin.
Schema 1 does not rotate trust, infer a globally current head, or migrate a
standalone publication implicitly.

The v4.6 coordinated-reservation-agreement schema 1 is an independent
checksummed JSON file. It binds one separated occupancy payload and exact
participant trust/publication prefixes into a canonical protocol round.
Loading validates structure, checksum, and internal identity. Semantic replay
requires the external exact occupancy bundle and every caller-mapped rotating
history. No standalone publication or history is enrolled implicitly, and no
globally newest round, consensus, or execution authority is inferred.

## Identity compatibility

Certificates and Atlases bind SHA-256 digests of canonical robot and scene
content plus validator name, validator version, and parameters. Advanced
corridor certificates additionally bind the exact OBB, portal, or route
subject. Compatibility means exact digest equality, not merely equal
dimensions or names.

Use `SafeAtlas::verify_compatible(robot, scene)`,
`HipacCorridor::verify_compatible(robot, scene)`, or
`RegionDatabase::verify_compatible(robot, scene)` before reuse. Consumers must
not bypass a mismatch by editing a manifest or substituting an obstacle set.
`SafetyMemory::query_reuse` applies the same rule to deployment, robot, and
scene identities; only `Direct` candidates may be recorded as reused.

An inherited schema-2 regional certificate additionally binds its parent
certificate and canonical `SceneDelta`. `AtlasVersionStore` verifies that the
parent exists, the scene transition joins the exact parent and child, and the
inherited link dependency is unchanged.

## Determinism

For identical inputs, options, schema, and library version, region IDs, region
order, graph structure, certificates, updates, version IDs, and payload bytes
are deterministic across supported thread counts. Fixed schema-2 payload
hashes, committed memory/store/fleet-archive/attestation/calibration-profile/
calibration-lifecycle/artifact-transfer-journal/service-trust-bundle/
service-trust-history/service-trust-checkpoint/reviewed-deployment-profile/
bounded-execution-session/execution-ledger/transparency-log/
transparency-gossip-archive/continuous-fleet-occupancy schema-1/schema-2 fixtures,
authenticated-occupancy-publication schema-1 fixtures,
occupancy-publication-history schema-1 fixtures,
coordinated-reservation-agreement schema-1 fixtures,
and the v0.5 schema-1 Atlas fixture
enforce interoperability across CI platforms.

Floating-point behavior is tested against conservative containment properties,
registered legacy golden fixtures, and named v1.0 release cases. The release
benchmark's committed cross-platform logical digest excludes timing, memory
estimates, and floating-point-derived identities. Determinism does not turn an
unsupported platform or altered compiler math mode into a supported target.
