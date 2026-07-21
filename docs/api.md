# API overview

The aggregate header is `#include <rbfsafe/rbfsafe.h>`. Consumers may include
module headers directly and link only the corresponding CMake target.

## Data and identity

- `Interval`, `Configuration`, `CspaceAabb`, and `WorkspaceAabb` are
  standard-library value types.
- `SerialRobotModel` stores modified-DH joints, limits, an optional constant
  tool transform, and one radius per transform-generated link.
- `SceneSnapshot` stores uniquely identified AABB obstacles and a required
  version string.
- Robot and scene `digest()` values are SHA-256 of canonical JSON content.

`SerialRobotModel::forward_kinematics(q)` returns `link_count() + 1` frame
origins, beginning with the base origin.

## Geometry and certification

`compute_ifk_aa_link_envelope(robot, domain)` returns one conservative
workspace AABB per represented link. `IfkAaLinkAabbValidator` returns either
`CertifiedFree` with a clearance lower bound or `Undetermined`. Envelope
overlap never proves collision.

Certificate evidence levels are explicit:

| Level | Meaning in v0.1 |
|---|---|
| `Unknown` | No accepted evidence |
| `PointChecked` | A point was checked; never a regional claim |
| `CertifiedRegion` | Every configuration in one C-space AABB passed the validator |
| `CertifiedConnectivity` | Reserved for stronger future connectivity evidence |
| `RuntimeExecutable` | Reserved for runtime execution guarantees |

The v0.1 builder issues only `CertifiedRegion` certificates. Atlas components
are query metadata and are not upgraded to `RuntimeExecutable`.

## LECT

`LectTree::create(root, policy)` creates a deterministic mutable partition.
`LectNodeKey` is the binary split path from the root (`""`, `"0"`, `"01"`,
and so on), not an internal array index.

Public operations include `split`, `node`, `locate`, `overlap_leaves`,
`leaf_keys`, traversal through `all_nodes`, and standalone save/open.
`LectSnapshot` exposes the same read-only query surface.

## Atlas construction

`AtlasBuilder::build(robot, scene, samples, options)` returns an
`AtlasBuildResult` containing the immutable-query Atlas and `BuildStats`.

| `BuildOptions` field | Default | Effect |
|---|---:|---|
| `maximum_depth` | `24` | Maximum split-path depth |
| `maximum_nodes` | `1,000,000` | Hard LECT node budget |
| `minimum_normalized_width` | `1e-3` | Stop threshold relative to the root width |
| `adjacency_tolerance` | `1e-12` | Region contact/overlap tolerance |
| `obstacle_padding` | `0` | Extra radius applied during validation |
| `threads` | `1` | Requested validation concurrency; output remains deterministic |
| `cancellation` | fresh token | Cooperative cancellation source |

Samples are validated, lexicographically sorted, and deduplicated. An empty
sample set is rejected. Reaching the node budget returns `ResourceLimit`;
depth or width limits leave affected branches unresolved and are reflected in
`BuildStats`.

## Atlas queries and persistence

- `regions_at(q)` returns every certified region containing `q`.
- `contains(q)` is a convenience membership predicate.
- `nearest_region(q)` returns the nearest C-space box, not a certified route.
- `connected(q1, q2)` tests stored component membership and does not generate a
  path.
- `verify_compatible(robot, scene)` checks exact identity digests.
- `save(path)` publishes schema v1 without overwriting by default.
- `load(path)` validates schema, bounds, counts, checksums, graph invariants,
  certificate identities, and trailing bytes before returning an Atlas.

## Error model

All expected C++ failures use `Result<T>` with one of `InvalidArgument`,
`DimensionMismatch`, `ResourceLimit`, `IdentityMismatch`,
`IncompatibleFormat`, `CorruptData`, `IoError`, `Cancelled`, or
`InternalError`.

Python raises `ValueError` for invalid arguments, `OSError` for I/O,
`MemoryError` for resource limits, and the `RBFSafeError` subclasses
`IdentityMismatchError`, `IncompatibleFormatError`, `CorruptDataError`,
`CancelledError`, or `InternalError` for the remaining categories.
