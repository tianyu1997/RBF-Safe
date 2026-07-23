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
to `RBFSafe::policy`. Documented public C++
declarations, installed CMake target names, and high-level Python names remain
source compatible throughout the 3.x line. Additive API changes may appear in
minor releases. Deprecated APIs remain functional through 3.x and may be
removed only in 4.0. The exact policy and automated review gate are documented
in [API stability](api-stability.md).

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
hashes, committed memory/store/fleet-archive/attestation/calibration fixtures,
and the v0.5 schema-1 Atlas fixture
enforce interoperability across CI platforms.

Floating-point behavior is tested against conservative containment properties,
registered legacy golden fixtures, and named v1.0 release cases. The release
benchmark's committed cross-platform logical digest excludes timing, memory
estimates, and floating-point-derived identities. Determinism does not turn an
unsupported platform or altered compiler math mode into a supported target.
