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

| Level | Meaning |
|---|---|
| `Unknown` | No accepted evidence |
| `PointChecked` | A point was checked; never a regional claim |
| `CertifiedRegion` | Every configuration in one C-space AABB passed the validator |
| `CertifiedConnectivity` | A subject-bound convex-cell/portal chain proves connectivity |
| `RuntimeExecutable` | Reserved for runtime execution guarantees |

`AtlasBuilder` issues only `CertifiedRegion` certificates. Atlas components
remain query metadata. The v0.4 corridor layer issues `CertifiedConnectivity`
only for explicit OBB/portal subjects. Neither layer issues
`RuntimeExecutable`.

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

Membership and nearest-region calls use a deterministic immutable BVH rebuilt
after Atlas construction or loading. Candidate results retain stable region
order, and the index is not serialized or included in schema identity.

## Trajectory auditing

`TrajectoryAuditor::audit(atlas, trajectory, options)` checks continuous
piecewise-linear coverage by analytically intersecting each segment with the
Atlas regions. The result is a `TrajectoryAuditReport` containing:

- `status`: `Certified`, `Partial`, or `Invalid`;
- equal-segment-parameter `coverage_ratio`;
- a deterministic `region_sequence`;
- explicit `TrajectoryInterval` gaps; and
- waypoint, segment, and region-test counts.

`Invalid` means the trajectory has no certificate coverage; it does not prove
collision. Invalid input remains a `Result<T>` failure. See the
[trajectory auditor guide](trajectory-auditor.md) for precise semantics.
`TrajectoryAuditOptions` defaults to ten million region/segment tests and a
fresh cooperative cancellation token.

## Optional OMPL adapter

Include `<rbfsafe/ompl.h>` and link `RBFSafe::ompl`. `make_ompl_state_space`
creates a bounded `ompl::base::RealVectorStateSpace` from the Atlas root.
`OmplAdapter::install` must run before `SpaceInformation::setup()` and installs:

- a state validity checker that returns true only for certified Atlas states;
- a motion validator that requires continuous certified coverage of each edge;
- a state sampler that draws from certified regions; and
- atomic query, motion, sampling, fallback, and audit-failure counters.

The adapter holds a shared immutable Atlas. Its bounds and dimensions must
exactly match the OMPL real-vector space. Unknown coverage returns false; v0.4
has no fallback collision checker. See the [OMPL adapter guide](ompl-adapter.md).

## OBB corridors and HiPaC

Include `<rbfsafe/corridor.h>` and link `RBFSafe::corridor`.

- `CspaceObb::create` validates a center, row-major orthonormal basis, and
  half-width vector.
- `ObbGenerator::segment_tube` constructs a deterministic oriented tube around
  a configuration-space segment.
- `ObbRegionValidator` certifies only when the OBB's conservative AABB
  enclosure passes `IFK-AA + LinkIAABB`.
- `ObbGrower` expands a certified segment tube toward a configured lateral
  cap under iteration, validation, and cancellation limits.
- `HipacCorridorBuilder::build` recursively covers a candidate path and returns
  coverage status, gaps, statistics, cells, and witness portals.
- `HipacCorridor` provides membership, certified connectivity, deterministic
  route recovery, identity checking, and save/load.

Advanced certificates populate `Certificate::subject_digest` with the SHA-256
of the exact OBB, portal, or route subject. Legacy Atlas schema-1 certificates
leave this optional field empty, preserving their identities. See the
[corridor guide](corridors.md) and [corridor schema](corridor-format.md).

## Error model

All expected C++ failures use `Result<T>` with one of `InvalidArgument`,
`DimensionMismatch`, `ResourceLimit`, `IdentityMismatch`,
`IncompatibleFormat`, `CorruptData`, `IoError`, `Cancelled`, or
`InternalError`.

Python raises `ValueError` for invalid arguments, `OSError` for I/O,
`MemoryError` for resource limits, and the `RBFSafeError` subclasses
`IdentityMismatchError`, `IncompatibleFormatError`, `CorruptDataError`,
`CancelledError`, or `InternalError` for the remaining categories.
