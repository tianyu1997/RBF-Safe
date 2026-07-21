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

The v0.2 reader accepts only schema 1. Unknown schemas return
`IncompatibleFormat`; malformed schema-1 data returns `CorruptData` or
`ResourceLimit`. Legacy RapidBoxForest caches are never interpreted as
RBF-Safe data.

The v0.2 library adds trajectory-audit APIs without changing Atlas schema 1,
LECT schema 1, or the robot and scene JSON schemas. A v0.1 Atlas remains
loadable when its schema and checksums are valid.

## Identity compatibility

Certificates and Atlases bind SHA-256 digests of canonical robot and scene
content plus validator name, validator version, and parameters. Compatibility
means exact digest equality, not merely equal dimensions or names.

Use `SafeAtlas::verify_compatible(robot, scene)` before reuse. Consumers must
not bypass a mismatch by editing a manifest or substituting an obstacle set.

## Determinism

For identical inputs, options, schema, and library version, region IDs, region
order, graph structure, certificates, and payload bytes are deterministic
across supported thread counts. Fixed payload hashes in the test suite enforce
schema-v1 interoperability across CI platforms.

Floating-point behavior is tested against conservative containment properties
and registered legacy golden fixtures. Determinism does not turn an
unsupported platform or altered compiler math mode into a supported target.
