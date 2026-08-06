# API overview

The aggregate header is `#include <rbfsafe/rbfsafe.h>`. Consumers may include
module headers directly and link only the corresponding CMake target.

## Data and identity

- `Interval`, `Configuration`, `CspaceAabb`, `WorkspaceAabb`, `WorkspaceObb`,
  `WorkspaceKdop`, `WorkspaceSupportHull`, and `WorkspaceEnvelope` are
  standard-library value types.
- `SerialRobotModel` stores modified-DH joints, limits, an optional constant
  tool transform, and one radius per transform-generated link.
- `SceneSnapshot` stores uniquely identified typed workspace envelopes and a
  required version string.
- Robot and scene `digest()` values are SHA-256 of canonical JSON content.

`SerialRobotModel::forward_kinematics(q)` returns `link_count() + 1` frame
origins, beginning with the base origin. `end_effector_pose(q)` returns a
`Pose3d` with position and an `x,y,z,w` unit quaternion.
`end_effector_geometric_jacobian(q)` returns a valid 6-by-N row-major
`GeometricJacobian` in the workspace frame. Its first three rows map joint
rates to linear end-effector velocity and its final three rows map them to
angular velocity. `at(row, column)` is bounds checked. See
[kinematics](kinematics.md) for the modified-DH convention.

## Geometry and certification

`compute_endpoint_aabbs(robot, domain, options)` selects certified IFK-AA or
non-certified CritSample endpoint generation. The returned
`EndpointAabbResult` uses paired proximal/distal boxes and explicitly reports
its source, certification status, and evaluated configuration count.

`compute_ifk_aa_link_envelope(robot, domain)` returns one conservative
workspace AABB per represented link. `IfkAaLinkAabbValidator` returns either
`CertifiedFree` with a clearance lower bound or `Undetermined`. Envelope
overlap never proves collision.

`compute_workspace_link_envelope(robot, domain, options)` uses the selected
endpoint source. Its `endpoint_bounds_certified` field must be checked before
the result is used as conservative evidence; CritSample results are diagnostic.

`compute_ifk_aa_workspace_link_envelope(robot, domain, options)` selects AABB,
OBB, standard k-DOP, or SupportHull output.
`IfkAaWorkspaceEnvelopeValidator` validates with the selected representation
while retaining enclosing-AABB Atlas dependencies. See
[workspace envelopes](workspace-envelopes.md).

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
convergence remains `PointChecked`. An exact command returned by a fully
verified `BoundedExecutionSession::authorize_command`, or by the ordered
`ExecutionLedger::authorize_command`, can carry `RuntimeExecutable`; the
session, ledger, transparency values, summaries, audit reports, and every
upstream artifact remain lower evidence.

## Continuous-time fleet occupancy

Include `<rbfsafe/occupancy.h>` and link `RBFSafe::occupancy`.
`build_robot_trajectory_occupancy_in_frame` subdivides a timestamped
piecewise-linear joint trajectory and derives one conservative IFK-AA
workspace AABB per link and time slice. `DeploymentFrameBounds` applies a
right-handed nominal rotation and translation, then expands the result for
axis-wise translation and arbitrary-axis angular uncertainty. Every occupancy
binds the exact robot digest, caller-defined timeline and workspace frame,
deployment ID, frame bounds, construction parameters, trajectory, and
deterministic slice identities. The preserved
`build_robot_trajectory_occupancy` translation-only API retains schema-1
semantics.

`verify_robot_trajectory_occupancy` replays the complete subdivision and
envelope computation against an exact robot model.
`analyze_continuous_fleet_occupancy` compares all temporally overlapping link
pairs under explicit time-sweep, link-pair, and conflict limits and returns either
`CertifiedSeparatedUnderSweptEnvelopes` or deterministic
`PotentialConflict` witnesses. `ContinuousFleetOccupancyBundle` provides
bounded, checksummed schema-1/schema-2 save/load and replays the report on
load.

The status is not a `Certificate`: every value remains `Unknown`, and none
authorizes execution. Obstacle freedom, self-collision, dynamics, clocks,
moving frames, time-varying localization, tracking, command transport, and
hardware interlocks remain separate. See
[the complete contract](continuous-fleet-occupancy.md)
and [storage format](continuous-fleet-occupancy-format.md).

## Continuous moving-obstacle occupancy

`build_moving_obstacle_occupancy` converts timestamped piecewise-linear
workspace AABB waypoints into deterministic, outward-rounded padded swept
slices. `verify_moving_obstacle_occupancy` reconstructs the full record.

`analyze_continuous_robot_scene_occupancy` requires complete timeline,
workspace-frame, and begin/end-window equality across all robot and obstacle
occupancies. It reports deterministic link/obstacle overlap or
separation-margin witnesses under explicit count, sweep, link, conflict, and
cancellation budgets.

`ContinuousRobotSceneOccupancyBundle` canonically stores all records and the
replayable report in an independent checksummed schema-1 file. Load verifies
moving-obstacle construction and replays analysis; each robot still requires
external exact-model replay. Every value and successful separation status
remains `Unknown` and non-authorizing. See
[the complete contract](continuous-moving-obstacles.md) and
[storage format](continuous-robot-scene-occupancy-format.md).

## Authenticated occupancy publication

Include `<rbfsafe/coordination.h>` and link `RBFSafe::coordination`.
`sign_continuous_fleet_occupancy_publication` signs one exact validated
occupancy-bundle file with an active, publication-authorized Ed25519 service
key. `OccupancyPublication` binds the payload length and SHA-256, decoded
bundle/timeline/frame identities, stream and publisher identities, exact trust
bundle, positive sequence and parent, and a closed tick window covered by
every deployment trajectory.

`verify_continuous_fleet_occupancy_publication` requires the exact payload,
public trust bundle, caller-pinned stream, publisher, trust-bundle ID, retained
parent, and evaluation tick. It verifies the signature and every binding from
one bounded payload read. `verify_occupancy_publication_successor` checks exact
sequence/parent extension and stable stream/publisher/timeline/frame, but
callers must authenticate both publications separately.

`OccupancyPublication::save/load` provides an independent checksummed
schema-1 file with bounded loading, atomic publication, and overwrite/symlink
guards. Publication verification never raises geometric evidence or
authorizes execution. See
[the complete contract](authenticated-occupancy-publication.md) and
[storage format](authenticated-occupancy-publication-format.md).

## Occupancy publication history

`OccupancyPublicationHistory::create/open` requires caller-pinned stream,
publisher, trust-bundle, root-publication, and expected-head identities. A
history stores one exact public trust snapshot plus immutable record,
publication, and payload files. Loading independently authenticates every
publication from its stored bytes and reconstructs the head.

`publish(publication, payload, expected_head)` serializes writers with a
directory lock, reopens under the expected head, verifies exact succession,
and commits the record last. `publication`, `current_publication`, and
`verify(publication_id, evaluation_tick)` provide bounded historical lookup
and tick-specific authentication.

`audit_occupancy_publication_histories` compares two valid histories under the
same pinned root and returns `Identical`, `FirstExtendsSecond`,
`SecondExtendsFirst`, or `Forked`, with a deterministic common-prefix report.
It detects only branches supplied to the call and does not choose a canonical
branch. History schema 1 fixes one trust bundle; trust rotation, remote
replication, consensus, and head distribution remain external. See
[the complete contract](occupancy-publication-history.md) and
[storage format](occupancy-publication-history-format.md).

## Trust-rotating occupancy publication history

`RotatingOccupancyPublicationHistory::create` takes a signed root
publication, exact payload, and complete caller-pinned `ServiceTrustHistory`.
It embeds a replayed copy of the trust chain and creates a separate schema-1
directory. The original fixed-trust history API and format remain unchanged.

`open` always pins stream, publisher, trust root, publication root, and
publication head. One overload pins the current trust bundle ID; another
accepts a complete `ServiceTrustCheckpoint` and separately retained
checkpoint ID. `rotate_trust` delegates single-signature or canonical quorum
successor verification to the service-trust protocol. `publish` requires both
expected heads and accepts only a publication under the exact current trust
bundle.

Replay resolves every publication's `trust_bundle_id` to its historical
bundle, authenticates its exact bytes, and rejects any backward bundle
transition. `audit_rotating_occupancy_publication_histories` returns separate
trust and publication relations, common prefixes, and a combined fork flag.
It neither discovers nor selects a globally current view.

`RotatingOccupancyPublicationHistoryLoadOptions` bounds both nested trust
replay and occupancy decoding plus aggregate history bytes. All returned
values remain `Unknown` and non-authorizing. See
[the complete contract](rotating-occupancy-publication-history.md) and
[storage format](rotating-occupancy-publication-history-format.md).

## Coordinated reservation agreement

`make_coordinated_reservation_agreement` requires one unique deployment and
independent `RotatingOccupancyPublicationHistory` for every participant. It
verifies that every selected publication authenticates the exact same
`CertifiedSeparatedUnderSweptEnvelopes` continuous-fleet payload at the
evaluation tick, then canonically binds deployment/occupancy, publisher/
stream/key/sequence, trust/publication prefixes, verification identities,
timeline/frame, common validity window, separation report, protocol, round,
and parent.

`verify_coordinated_reservation_agreement` recomputes the agreement from the
exact occupancy bundle and explicit deployment-to-history mapping. Retained
participant heads may be prefixes of later-extended histories.
`verify_coordinated_reservation_successor` additionally requires the current
deployment-to-history mapping. It proves stable protocol and membership,
exact parent and next round, nondecreasing evaluation time, a strictly newer
publication sequence from every participant, publication-prefix extension,
and non-regressing trust prefixes. A higher sequence on a fork is rejected.

`CoordinatedReservationAgreement::save/load` provides a bounded checksummed
schema-1 file. The caller must compare a separately retained agreement ID.
Every participant and agreement remains `Unknown` and non-authorizing. See
[the complete contract](coordinated-reservation-agreements.md) and
[storage format](coordinated-reservation-agreement-format.md).

## Revocation-aware execution ledger

Include `<rbfsafe/execution_ledger.h>` and link `RBFSafe::execution`.
`ExecutionLedger` persists strict command authorization/completion order with
an expected record head. Each authorization revalidates a caller-pinned
current trust checkpoint and the original reviewer keys. Signed controller
completion, cancellation, closed-session expiration, and exact
profile/Atlas/scene/endpoint/reviewer/checkpoint revocations are immutable
records. `audit` replays the session, checkpoints, signatures, record chain,
and derived terminal state offline. See
[the schema-1 format and safety contract](execution-ledger-format.md).

## Deployment and runtime transparency

Include `<rbfsafe/transparency.h>` and link `RBFSafe::transparency`.
`DeploymentTransparencyAnchor::create` binds one exact reviewed profile and
approval set to the caller-pinned trust root, signed checkpoint, current bundle
sequence, and replayed history head.

`IndependentRuntimeObservation::create` accepts only the authorization that is
currently outstanding in an `ExecutionLedger`. It binds the exact session,
ledger head, command index/digest, caller-supplied runtime snapshot,
configuration digest, monitor state, and a caller-supplied monotonic time
inside the command's closed authorization window.
`sign_runtime_observation`, `assemble_runtime_observation_attestations`, and
`verify_runtime_observation_attestations` provide canonical Ed25519 source
quorums under the current caller-supplied trust bundle. Duplicate keys,
insufficient or non-distinct services, inactive/non-publication keys, and the
controller service are rejected by default.

`TransparencyLog` stores deployment anchors and observation sets as
deterministic Merkle leaves. `publish_deployment_anchor` and
`publish_runtime_observation` require the expected current checkpoint and the
log signing secret. `inclusion_proof`, `consistency_witness`,
`compact_consistency_proof`, and `audit` verify retained history; `open`
additionally requires the exact caller-pinned log identity and expected
checkpoint. The original consistency witness remains an explicit bounded
ordered leaf list. The 3.14 compact proof instead carries the old
complete-subtree frontier and aligned appended subtrees.

Every anchor, observation, attestation set, log, proof, checkpoint, and audit
is `Unknown` and non-authorizing. See
[the transparency format and trust boundary](transparency-log-format.md).

## Witnessed transparency and checkpoint gossip

Include `<rbfsafe/witness.h>` and link `RBFSafe::witness`.
`sign_transparency_checkpoint_witness` binds an independent Ed25519 witness to
one exact log checkpoint and caller-supplied trust bundle.
`assemble_witnessed_transparency_checkpoint` enforces a canonical quorum; the
default requires two distinct services and excludes the log signer.

`sign_transparency_checkpoint_gossip` authenticates a witnessed checkpoint,
optional compact proof, sender sequence/parent, recipient, and exact trust
bundle. `audit_transparency_checkpoint_gossip` verifies all messages, builds a
compact-proof reachability graph, and returns `Consistent`, `Incomplete`, or
`SplitView`, with explicit same-size-equivocation and invalid-proof conflicts.

`TransparencyGossipArchive` provides bounded schema-1 immutable records,
expected-head publication, cross-process writer exclusion, complete replay,
and caller-pinned log/trust identity. It does not discover peers, send network
messages, select a newest checkpoint, or rotate its pinned bundle. See
[witnessed transparency and gossip](witnessed-transparency.md).

## Verifiable provenance and external time

Include `<rbfsafe/provenance.h>` and link `RBFSafe::provenance`.
`sign_hardware_key_attestation_statement` normalizes no vendor format itself;
it signs the caller's exact adapter/version/format, vendor/product,
evidence/nonce digests, subject key, usages, parent, and trust-bundle binding.
`HardwareKeyProvenancePolicy` explicitly pins the accepted adapters,
authorities, vendors, required scopes, quorum, distinctness, and chain bound.
`replay_hardware_key_provenance` verifies one exact subject-key chain.

`sign_external_time_assertion` creates per-source parent-linked values under an
explicit clock namespace. `evaluate_external_time_freshness` verifies the
source chains and trust bundle, intersects their uncertainty intervals, and
returns `Fresh`, `Incomplete`, `Stale`, `Future`, or `Inconsistent` against a
caller-supplied evaluation time. The library never reads a system clock.

`VerifiableProvenanceBundle` combines both policies and complete replay inputs
in a checksummed bounded schema-1 file. `replay_verifiable_provenance` returns
both reports; `ready()` is convenience metadata only. Every value remains
non-authorizing `Unknown`. See
[the provenance trust boundary](verifiable-provenance.md).

## Reviewed deployment profiles

Include `<rbfsafe/deployment.h>` and link `RBFSafe::deployment`.

`DeploymentProfile::create(input)` canonicalizes required review roles and
binds deployment, robot, controller, platform, runtime-build, trust-root,
signed-checkpoint, trust-head, runtime constraints, and review policy into one
deterministic SHA-256 identity.

`sign_deployment_profile_approval` creates a role-bound Ed25519 approval.
`assemble_deployment_profile_approvals` sorts approvals, rejects duplicate
signers, and enforces minimum, distinct-service, and required-role policy.
`ReviewedDeploymentProfile::create` additionally verifies the exact
caller-pinned checkpoint/history/head and every approval against active,
publication-capable keys.

`assess(snapshot)` returns a deterministic `Conformant` or `Nonconformant`
`DeploymentProfileAssessment`. It checks all identities, observation age,
command latency, control period, missed cycles, monitor presence,
fail-closed transport, and authenticated-artifact status. Assessments carry
`Unknown` evidence and `authorizes_execution()` is always false.

`save` and `load` use the independent bounded
reviewed-deployment-profile schema 1. Loading requires the trust history,
checkpoint, and caller-retained checkpoint ID; no self-consistent file is
trusted automatically. See
[reviewed deployment profiles](deployment-profile-format.md).

## Bounded execution sessions

Include `<rbfsafe/execution.h>` and link `RBFSafe::execution`.

`ExecutionCommandSequence::create(atlas, configurations, offsets)` requires at
least two finite configurations, an offset-zero strictly increasing schedule,
continuous trajectory certification, and an Atlas connectivity certificate.
`verify_compatible` repeats the audit and requires the exact region sequence,
Atlas identity, robot/scene identities, and connectivity-certificate ID.

`ExecutionSessionRequest::create` binds that sequence to one exact reviewed
profile, trust checkpoint/head, distinct controller/runtime-monitor Ed25519
endpoint keys, a SHA-256 session nonce, and bounded start/duration/command
limits. New role-bound execution approvals must be signed by the exact
reviewers that approved the profile. Separate controller and monitor
acknowledgements bind the request, accepted command count, runtime snapshot,
monotonic observation time, and armed state.

`BoundedExecutionSession::create` reverifies the entire chain. The session
reports `Unknown` and never authorizes execution. `authorize_command` returns
an optional `ExecutionCommandAuthorization` with `RuntimeExecutable` only for
the exact index/configuration inside its closed command window. `save`/`load`
use bounded atomic schema-1 JSON and loading requires the exact reviewed
profile, trust history/checkpoint caller anchor, and Atlas.

See [bounded execution sessions](bounded-execution-session-format.md).

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

`PolicyCalibrationProfile::create(input)` validates contiguous confidence
bins and binds policy/model, deployment scope, task, dataset, method, outcome,
and uncertainty-unit identities. It derives observed success rates, 95%
Wilson lower bounds, expected calibration error, maximum bin error, sample
count, and a deterministic profile ID.

`profile.lookup(raw_confidence)` returns the matching bin plus calibrated and
conservative confidence. `CalibratedPolicySafetyGate::check_proposals(...)`
requires trusted expected scope/model identities, enforces profile quality
gates, records raw/effective metadata, and delegates effective proposals to
`LearningPolicySafetyGate`. Profile `save`/`load` use bounded schema-1 JSON.
See [policy calibration](policy-calibration.md).

`assess_policy_calibration_drift(profile, window, options)` compares aggregate
operational outcomes with the exact profile baseline. It derives confidence-
distribution total variation distance, live calibration errors, overall
success-rate drop, and maximum per-bin success-rate drop, then returns
`InsufficientData`, `Stable`, or `DriftDetected` with explicit reasons and a
deterministic report ID.

`PolicyCalibrationLifecycle::create(profile)` starts a profile-bound
`PendingReview` history. `assess(...)` appends a drift report and automatically
quarantines detected drift; `transition(...)` performs reviewed manual state
changes under an expected-head precondition. Activation requires the latest
assessment to be stable, quarantined histories must return through pending
review, and retired histories are terminal. Every event binds its parent and
receives a deterministic ID.

`CalibratedPolicySafetyGate::check_proposals_guarded(...)` requires the exact
lifecycle to be valid, active, stable, and equal to a trusted expected head
before invoking the complete calibrated gate. Lifecycle `save`/`load` use an
independent bounded schema-1 JSON file and replay all reports, transitions,
IDs, and parent links. Neither drift status nor lifecycle state raises
evidence to `RuntimeExecutable`. See
[policy calibration drift and lifecycle](policy-calibration-lifecycle.md).

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
`FleetScheduleArchive` publishes those reports into a deterministic linear
history. Each `FleetScheduleVersion` binds its sequence, parent, exact fleet
snapshot, report, and `SafetyMemory::identity()`. Publication requires an
expected head and is idempotent for unchanged content; `verify_version`
replays the stored reservations against the exact supplied fleet and memory.
`save` and `load` use the independently versioned, checksummed and bounded
fleet-schedule-archive schema 1.
Its `ConflictFreeUnderDeclaredEnvelopes` status is not a `Certificate` or
execution authorization. See [persistent safety memory](safety-memory.md) and
the [memory format](safety-memory-format.md). Multi-process deployments should
also read the [transactional store contract](safety-memory-store.md) and
[fleet archive contract](fleet-schedule-archive.md).

## Artifact authentication

Include `<rbfsafe/trust.h>` and link `RBFSafe::trust`.
`attest_artifact` and `attest_artifact_file` create deterministic,
full-length HMAC-SHA256 sidecars for exact `MemoryArtifact` lifecycle metadata
and payload bytes. `verify_artifact` and `verify_artifact_file` require the
service ID, key ID, and shared key selected by trusted application
configuration; an untrusted sidecar cannot choose its own trust anchor.

`ArtifactVerificationOptions` bounds payload and metadata bytes and carries a
cancellation token. `save_artifact_attestation` and
`load_artifact_attestation` use independent schema 1. Loading validates
structure and deterministic identity only; authentication occurs exclusively
in a verify call with an external key. See
[authenticated artifact attestations](artifact-attestation.md).

## Remote artifact transfers

Include `<rbfsafe/remote.h>` and link `RBFSafe::remote`.
`prepare_artifact_fetch` and `prepare_artifact_publish` capture the exact
whole-memory identity, artifact lifecycle, service, locator, media type,
resource cap, and expected authentication policy in deterministic requests.
For this API, the artifact content digest must equal SHA-256 of the exact
payload bytes.

Service adapters construct a response or receipt with
`make_artifact_fetch_response` or `make_artifact_publish_receipt`, then apply
`authenticate_artifact_fetch_response` or
`authenticate_artifact_publish_receipt`. The transfer attestation HMAC binds
the request ID, response/receipt ID, operation, service/key identity, artifact,
payload digest/count, and service sequence. Clients select the expected key
from trusted configuration and call `verify_artifact_fetch` or
`verify_artifact_publish`; the verify call rechecks the current memory and
exact bytes before returning `VerifiedArtifactTransfer`.

`ArtifactTransferJournal::append` requires an expected head and stores compact
verified-transfer metadata. Current writers use a bounded checksummed
schema-2 directory; readers retain schema-1 compatibility. Neither a verified
transfer nor a journal record parses payload semantics or raises evidence.
See the
[remote artifact service contract](remote-artifact-service.md) and
[journal format](artifact-transfer-journal-format.md).

## Public-key service identities

Include `<rbfsafe/identity.h>` and link `RBFSafe::identity`.
`ed25519_key_pair_from_seed`, `ed25519_sign`, and `ed25519_verify` expose the
RFC 8032 primitive needed by service adapters. Deterministic examples use a
fixed 32-byte seed; production callers must supply securely generated,
externally protected keys.

`make_service_public_key` binds a service, Ed25519 public key, inclusive
service-sequence window, fetch/publish/rotate permissions, and lifecycle state.
`ServiceTrustBundle::create` produces a deterministic caller-pinnable root;
`ServiceTrustBundle::create_with_rotation_policy` produces schema 3 with an
immutable `ServiceTrustRotationPolicy` minimum-signature and optional
distinct-service requirement;
`rotate_service_trust_bundle` enforces a parent-linked monotonic successor and
one-way pending/active/retired/revoked transitions.
`authorize_service_trust_bundle_successor` and
`verify_service_trust_bundle_successor` bind an exact schema-2 transition to
one active predecessor key with rotation permission.
`assemble_service_trust_bundle_authorizations` canonicalizes multiple
independent signatures and the authorization-set verifier enforces schema-3
key/service quorum. `save`/`load` use bounded schema-1/2/3 JSON and never
persist a private key.

`ServiceTrustHistory::create` requires the caller's exact root pin.
`open` additionally requires the caller-retained expected head and replays
every immutable bundle, record, policy transition, and Ed25519 authorization.
`publish` locks, replays against its expected head, and atomically appends a
verified single authorization or canonical authorization set.
`ServiceTrustHistoryLoadOptions` bounds bundle count,
per-bundle/aggregate key count, signatures per rotation, JSON bytes, and cancellation. `records`,
`bundle`, and `current_bundle` expose the replayed public audit.

`sign_service_trust_checkpoint` signs the exact replayed root/head/sequence/
record with an eligible current-head rotation key.
`assemble_service_trust_checkpoint` enforces the current bundle's quorum and
produces a canonical schema-1 `ServiceTrustCheckpoint`. The checkpoint-pinned
`ServiceTrustHistory::open` overload and
`verify_service_trust_checkpoint` require a caller-retained checkpoint ID.
`ServiceTrustCheckpointLoadOptions` bounds standalone input bytes and
signature count.

Service adapters call `sign_artifact_fetch_response` or
`sign_artifact_publish_receipt`. Clients call
`verify_artifact_fetch_offline` or `verify_artifact_publish_offline` with the
exact locally authorized bundle. Successful transfers record
`verification_key_id` and `trust_bundle_id`; bundle integrity alone does not
authorize an unpinned root. See
[public-key service identities](public-service-identities.md), the
[trust-bundle format](service-trust-bundle-format.md), and the
[trust-history format](service-trust-history-format.md), and the
[trust-checkpoint format](service-trust-checkpoint-format.md).

## Error model

All expected C++ failures use `Result<T>` with one of `InvalidArgument`,
`DimensionMismatch`, `ResourceLimit`, `IdentityMismatch`,
`IncompatibleFormat`, `CorruptData`, `IoError`, `Cancelled`, or
`InternalError`.

Python raises `ValueError` for invalid arguments, `OSError` for I/O,
`MemoryError` for resource limits, and the `RBFSafeError` subclasses
`IdentityMismatchError`, `IncompatibleFormatError`, `CorruptDataError`,
`CancelledError`, or `InternalError` for the remaining categories.
