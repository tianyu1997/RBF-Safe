# Versioning and compatibility

## Library versions

RBF-Safe uses Semantic Versioning. Before 1.0, minor releases may make
documented source-incompatible C++ API changes. Patch releases should remain
source compatible within a minor line. C++ ABI stability is not promised
before 1.0.

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

An inherited schema-2 regional certificate additionally binds its parent
certificate and canonical `SceneDelta`. `AtlasVersionStore` verifies that the
parent exists, the scene transition joins the exact parent and child, and the
inherited link dependency is unchanged.

## Determinism

For identical inputs, options, schema, and library version, region IDs, region
order, graph structure, certificates, updates, version IDs, and payload bytes
are deterministic across supported thread counts. Fixed schema-2 payload
hashes plus a committed v0.5 schema-1 fixture enforce interoperability across
CI platforms.

Floating-point behavior is tested against conservative containment properties
and registered legacy golden fixtures. Determinism does not turn an
unsupported platform or altered compiler math mode into a supported target.
