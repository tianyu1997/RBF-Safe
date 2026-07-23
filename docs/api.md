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
origins, beginning with the base origin. `end_effector_pose(q)` returns a
`Pose3d` with position and an `x,y,z,w` unit quaternion.

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

`AtlasBuilder` and `AtlasUpdater` issue only `CertifiedRegion` certificates. A
custom `RegionValidator` must attach one valid conservative workspace AABB per
robot link to every `CertifiedFree` result; schema-2 Atlas construction rejects
incomplete dependencies. Corridor and Atlas route APIs issue
`CertifiedConnectivity` only for explicit cell/witness subjects. Safe IK pose
convergence remains `PointChecked`. No v3.0 component issues
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
- `route(q1, q2)` recovers a deterministic chain through exactly intersecting
  convex Atlas AABBs and returns witness waypoints plus a subject-bound
  `CertifiedConnectivity` certificate.
- `connected(q1, q2)` is true only when `route` can recover that certified
  chain; adjacency tolerance alone cannot bridge a geometric gap.
- `verify_compatible(robot, scene)` checks exact identity digests.
- `save(path)` publishes the Atlas's schema without overwriting by default;
  new builds use schema 2.
- `load(path)` accepts Atlas schemas 1 and 2 and validates bounds, counts,
  checksums, graph invariants, certificates, dependencies, transitions, and
  version identities.

Membership and nearest-region calls use a deterministic immutable BVH rebuilt
after Atlas construction or loading. Candidate results retain stable region
order, and the index is not serialized or included in schema identity.

## Dynamic scene updates

Include `<rbfsafe/dynamic.h>` and link `RBFSafe::update`.

- `compare_scenes(before, after)` returns a deterministic obstacle-ID
  `SceneDelta` with exact old/new bounds and identities.
- `AtlasUpdater::update` inherits a region only when its parent certificate,
  policy, subject, and stored link envelope prove every added/modified
  obstacle disjoint.
- Failed inheritance triggers direct validation. Undetermined regions are
  removed and optionally refined in bounded local LECTs.
- `repair_domains()` exposes unresolved domains retained for later recovery.
- `AtlasUpdateResult` reports retained, invalidated, and repaired IDs plus
  exact validation and repair statistics.

Initial Atlas versions have sequence zero. Derived versions bind the parent
version and complete scene transition. `AtlasVersionStore` publishes only a
valid child of the active head, loads historical versions, and atomically
rolls the head back without deleting descendants. See
[dynamic updates](dynamic-updates.md).

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
- an optional state sampler that draws from certified regions; and
- atomic query, motion, sampling, fallback, and audit-failure counters.

The adapter holds a shared immutable Atlas. Its bounds and dimensions must
exactly match the OMPL real-vector space. Unknown coverage returns false; there
is no fallback collision checker. `OmplPlanner` selects upstream RRT, RRT*,
PRM, or BIT*, optionally seeds PRM from `CertifiedRoadmap`, and independently
audits every returned path. See the [OMPL adapter guide](ompl-adapter.md).

## Certified planning primitives

Include `<rbfsafe/planning.h>` and link `RBFSafe::planning`.

- `CertifiedRegionSampler` provides seeded uniform or volume-weighted sampling
  and bounded certified near-sampling.
- `CertifiedRoadmapBuilder` creates region-center and exact-overlap-witness
  nodes; each edge is contained by one certified convex AABB.
- `CertifiedRoadmap` retains robot/scene identity, adjacency, nearest-node, and
  compatibility queries.

These are proposal/search structures, not new certificates. See
[certified planning consumers](planning-consumers.md).

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

## Safe IK

Include `<rbfsafe/safe_ik.h>` and link `RBFSafe::ik`.
`SafeIkSolver::solve(robot, scene, atlas, target, current, options)` searches
certified regions in deterministic seed-distance order and projects every
numerical step into the active region. Its `SafeIkReport` distinguishes:

- `SafeConnected`: pose-checked solution plus an Atlas route from `current`;
- `SafeUnconnected`: region-certified pose solution without such a route;
- `SeedNotCertified`: the current state is outside the Atlas; and
- `NoSolution`: bounded search found no tolerance-satisfying solution.

The report keeps the destination `CertifiedRegion`, the `PointChecked` pose
evidence, numerical errors/statistics, and, when present, the
`CertifiedConnectivity` route as separate claims. See [Safe IK](safe-ik.md).

## Generalized region database

Include `<rbfsafe/region_database.h>` and link `RBFSafe::regions`.

- `RegionDatabase` stores stable AABB, OBB, Portal, TrajectoryTube, zonotope,
  and Taylor records with separate certificate lookup and component labels.
- `from_atlas` and `from_corridor` import existing producers without changing
  their formats; `create` accepts independently certified primary regions.
- `ObbAtlasBuilder::build` grows point cells and nearest-neighbor segment
  tubes, then discovers arbitrary intersecting AABB/OBB pairs under hard
  budgets.
- `CspacePortal` retains the complete convex half-space intersection and a
  verified shared witness.
- `regions_at`, `contains`, `nearest_region`, `connected`, `region`, and
  `certificate` provide deterministic high-level queries.
- `save` and `load` use the independent checksummed region-database schema 1.

`CspaceZonotope`, `CspaceTaylorRegion`, and
`HigherOrderRegionValidator` preserve shared first-order variables through DH
FK and add conservative nonlinear remainders. A successful
`make_higher_order_region_certificate` binds the exact correlated region.
These APIs are experimental in v0.8; arbitrary Portal discovery currently
accepts only AABB/OBB parents. See the [region database guide](region-database.md)
and [format specification](region-database-format.md).

## Optimization consumers

Include `<rbfsafe/optimization.h>` and link `RBFSafe::optimization`.

`compile_region_constraint` maps AABB, OBB, and Portal geometry to direct
half-spaces and maps zonotope/Taylor geometry to lifted generator equalities
with bounded auxiliary variables. `assign_trajectory_regions` deterministically
matches waypoints. The TrajOpt, CHOMP, STOMP, and MPC adapters label identical
solver-neutral programs and expose residuals, gradients, and bounded cyclic
projection. `TrajectoryTube` unions must first be expanded into referenced
convex cells. A final `TrajectoryAuditor` pass remains mandatory. See
[optimization adapters](optimization.md).

## Runtime action shield

Include `<rbfsafe/shield.h>` and link `RBFSafe::shield`.
`RuntimeShield::check_action(robot, scene, atlas, current, action, options)`
accepts `JointDeltaAction`, `EndEffectorAction`, or `TrajectoryAction` through
the `ShieldAction` variant. It returns a `ShieldDecision` with a stable ID,
identity digests, requested joint target where applicable, exact output
trajectory, repair distance, final audit, endpoint connectivity certificate,
and evidence level.

`ShieldOutcome` is `Accept`, `Repair`, or `Reject`. Semantic rejection remains
a successful `Result<ShieldDecision>`; malformed data, identity mismatches,
cancellation, and exhausted computational budgets remain errors. Every
non-rejected output is independently audited and carries no evidence above
`CertifiedConnectivity`.

`check_actions` evaluates an ordered VLA/proposal batch from one state and
selects the first accepted decision, otherwise the first repaired decision.
`telemetry()` returns synchronized aggregate counters.
`RuntimeShieldMonitor` accepts only compatible certified decisions and
classifies timestamped observations as `OnCertifiedPlan`,
`CertifiedDeviation`, `UncertifiedState`, or `Inactive`.

Python exposes `RuntimeShield.check_action` plus typed
`check_joint_action`, `check_end_effector_action`, and
`check_trajectory_action` conveniences. See the
[runtime shield guide](runtime-shield.md) for complete semantics and limits.

## Learning-policy safety

Include `<rbfsafe/policy.h>` and link `RBFSafe::policy`.
`LearningPolicySafetyGate::check_proposals` accepts an ordered batch of
`PolicyProposal` values. Required policy/task identity, confidence,
state/action uncertainty, observation age, and inference latency are validated
before eligible actions enter `RuntimeShield`.

`PolicyGateOptions` defines deterministic metadata thresholds, a proposal
budget, nested `ShieldOptions`, and `InputOrder`, `HighestConfidence`, or
`LowestUncertainty` selection. Accepts are preferred over repairs. The
`PolicyBatchReport` retains every `PolicyGateDecision`, at most one selected
index, and one aligned `PolicyFeedbackRecord` per input. Stable SHA-256 IDs
cover action, metadata, decisions, identities, targets, labels, and evidence.

`PolicyFeedbackDatabase` validates and appends unique records under a hard
budget, queries by policy/task/episode/label, reports aggregate label counts,
and saves/loads an independent checksummed schema 1. Neither policy decisions
nor feedback exceed `CertifiedConnectivity`; they are not execution
authorization. See [learning-policy safety](policy-safety.md) and the
[feedback format](policy-feedback-format.md).

## Persistent safety memory and fleets

Include `<rbfsafe/memory.h>` and link `RBFSafe::memory`.
`SafetyMemory::register_artifact` stores deterministic identity metadata for an
external Atlas, region database, corridor, audit, policy-feedback database,
runtime trace, or fleet report. Artifacts expose `Active`, `Stale`,
`Quarantined`, and `Retired` lifecycle states plus optimistic generations.
Every registration, transition, scene invalidation, and accepted reuse is a
stable chronological `MemoryEvent`.

`assess_reuse` explains one candidate. `query_reuse` returns deterministic
direct and optionally revalidation-required candidates under exact deployment,
robot, scene, type, tag, task, and minimum-evidence rules. `record_reuse`
accepts only a direct candidate. `save` and `load` use the independent bounded,
checksummed safety-memory schema 1 and replay the complete history.
`identity` hashes the complete canonical memory state. `SafetyMemoryStore`
wraps immutable memory directories in a deterministic parent/revision chain;
`publish` requires the current revision observed by the caller, serializes
cross-process writers, and never overwrites a commit.

`make_fleet_snapshot` binds sorted fleet members to one scene.
`make_fleet_reservation` requires an active, compatible, region-certified
source artifact and bounds occupancy by the member operating envelope.
`analyze_fleet_schedule` rechecks every source against the current memory and
reports duplicate robot windows, declared workspace overlap, and
separation-margin violations under pair and cancellation budgets.
Its `ConflictFreeUnderDeclaredEnvelopes` status is not a `Certificate` or
execution authorization. See [persistent safety memory](safety-memory.md) and
the [memory format](safety-memory-format.md). Multi-process deployments should
also read the [transactional store contract](safety-memory-store.md).

## Error model

All expected C++ failures use `Result<T>` with one of `InvalidArgument`,
`DimensionMismatch`, `ResourceLimit`, `IdentityMismatch`,
`IncompatibleFormat`, `CorruptData`, `IoError`, `Cancelled`, or
`InternalError`.

Python raises `ValueError` for invalid arguments, `OSError` for I/O,
`MemoryError` for resource limits, and the `RBFSafeError` subclasses
`IdentityMismatchError`, `IncompatibleFormatError`, `CorruptDataError`,
`CancelledError`, or `InternalError` for the remaining categories.
