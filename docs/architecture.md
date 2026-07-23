# Architecture

RBF-Safe uses one-way dependencies so that geometry and partitioning remain
usable without the Atlas layer:

```text
RBFSafe::geometry
       -> RBFSafe::lect
       -> RBFSafe::atlas
          |-> RBFSafe::update
          |-> RBFSafe::memory
          |-> RBFSafe::ik -> RBFSafe::shield -> RBFSafe::policy
          |-> RBFSafe::planning -> RBFSafe::ompl (optional)
          `-> RBFSafe::corridor -> RBFSafe::regions
                                      `-> RBFSafe::optimization

All core targets -> RBFSafe::rbfsafe (aggregate target)
```

## Modules

### Geometry

Owns standard-library value types, modified-DH forward kinematics, robot and
scene canonical identity, affine-arithmetic endpoint propagation, conservative
LinkIAABB construction, and the default `RegionValidator`.

### LECT

Owns a deterministic binary partition over a C-space AABB. Split keys are bit
paths from the root, so public identity is independent of memory allocation or
array position. `LectTree` is mutable; `LectSnapshot` is read-only.

### Atlas

Owns certificates, seed-guided construction, safe regions, revalidated merges,
adjacency, connected components, an immutable region query BVH, schema-2
persistence, and continuous trajectory coverage auditing.

### Dynamic update

`RBFSafe::update` owns obstacle-ID scene differences, certificate inheritance,
local LECT repair, unresolved repair-domain recovery, and immutable Atlas
version stores. It consumes Atlas schema-2 link-envelope dependencies but does
not flow into geometry, LECT, or Atlas construction. Complete `SceneDelta`
records and parent certificate IDs keep derived claims auditable.

### Corridor

Owns standard-library C-space OBB values, deterministic segment-tube
generation, conservative OBB enclosure validation, bounded growth, HiPaC
recursive path covering, witness portals, connectivity route certificates,
and corridor schema-1 persistence. It depends on Atlas-layer cancellation,
trajectory interval, and save-option types, but does not mutate an Atlas.

### Safe IK

`RBFSafe::ik` owns deterministic projected damped-least-squares solving inside
certified Atlas regions. It consumes `Pose3d`, the robot/scene identity, and
an immutable Atlas. A successful connected result combines point-checked pose
evidence, the destination region certificate, and an explicit Atlas route
whose subject digest binds every region and witness waypoint.

### Generalized regions

`RBFSafe::regions` owns the unified region/certificate database, arbitrary
AABB/OBB half-space Portal discovery, deterministic OBB Atlas growth, and the
experimental correlation-preserving zonotope/Taylor IFK backend. It consumes
the Atlas and corridor public models but does not alter their storage schemas.
Its own schema-1 directory persists every generalized geometry and rebuilds
the connectivity graph during validated loading.

### Planning consumers

`RBFSafe::planning` owns a deterministic sampler over the certified Atlas
union and an in-memory certified roadmap. Roadmap center-to-portal edges are
accepted only when a single certified convex AABB covers each edge. The target
depends only on Atlas and remains independent of any third-party planner.

### Optimization consumers

`RBFSafe::optimization` compiles generalized regions into solver-neutral
linear constraints. AABB, OBB, and Portal records become half-space systems;
zonotope and Taylor records use bounded lifted variables. It also provides
trajectory-to-region assignment, residual evaluation, bounded projection, and
named TrajOpt, CHOMP, STOMP, and MPC front ends. These front ends produce
portable constraint data and do not link those external solvers.

### Runtime shield

`RBFSafe::shield` combines immutable Atlas queries, connected Safe IK, Atlas
route recovery, and the continuous trajectory auditor. It owns standard-value
action types, bounded component-constrained repair, ordered proposal batches,
telemetry, and the runtime observation monitor. It has no learned-model,
network, controller, ROS, or hardware dependency and does not issue
`RuntimeExecutable` evidence.

### Learning-policy safety

`RBFSafe::policy` adds uncertainty/latency gates and deterministic learned-
policy selection above `RBFSafe::shield`. It owns proposal metadata, aligned
training feedback, aggregate gate telemetry, and the independent checksummed
policy-feedback schema. It does not load model weights, call inference
services, update policies, execute commands, or promote shield evidence.

### Persistent safety memory

`RBFSafe::memory` depends only on the Atlas-level standard value, cancellation,
save-option, and evidence types. It owns the deterministic artifact catalog,
monotonic lifecycle state machine, chronological audit log, exact-identity
cross-task reuse assessment, safety-memory schema-1 reader/writer, fleet
identity, and conservative reservation conflict analysis. Artifact locators
remain opaque; the module neither opens referenced payloads nor converts their
certificates. Fleet analysis returns a coordination report rather than a
geometric or runtime certificate.

The 3.1 revision-store backend adds deterministic whole-memory identities and
an append-only commit chain. Each commit points to an immutable schema-1 memory
directory. An atomically created writer-lock directory serializes processes;
the writer reopens the store and verifies the caller's expected head before it
publishes a new immutable commit filename. Readers never consume temporary or
uncommitted revisions.

### Python and tools

pybind11 mirrors stable high-level operations and maps error categories to
Python exceptions. The C++ and Python `rbfsafe-inspect` tools load through the
same validating reader used by the library.

### Optional OMPL adapter

`RBFSafe::ompl` depends on `RBFSafe::planning` and OMPL, but the dependency does
not flow back into the core targets or Python wheels. It owns the conversion
between `RealVectorStateSpace` states and configurations, strict Atlas-backed
state validity, continuous edge validation through `TrajectoryAuditor`, and
certified-region sampling. Its high-level helper selects upstream RRT, RRT*,
PRM, or BIT*, can seed PRM with a revalidated certified roadmap, and audits
every returned path before reporting a certified exact solution.

### Optional MoveIt 2 integration

`plugins/moveit2/rbfsafe_moveit` is an independent ROS 2 Jazzy ament package.
It consumes the installed core through `find_package(RBFSafe)` and exports
request/response adapters, a certified constraint-sampler allocator, and a
`KinematicsBase` plugin. No ROS, URDF, MoveIt, or pluginlib type enters a core
public header or Python wheel.

The trajectory auditor remains in the Atlas layer: it consumes immutable
certified regions and produces a report without depending on robot geometry,
planners, or storage internals.

The corridor layer consumes robot and scene models directly. Its OBB validator
delegates the enclosing AABB to the geometry validator; no correlated OBB
state is passed into the affine-arithmetic kernel in v0.4.
The v0.7 higher-order backend is separate: it retains shared affine variables
through a first-order Taylor model and adds conservative nonlinear remainders.

## Construction flow

1. Validate and canonically identify the robot and scene.
2. Sort and deduplicate seed configurations.
3. Create a LECT rooted at the complete joint-limit domain.
4. Certify the root or split unresolved seeded nodes along the normalized
   longest axis, validating both children.
5. Stop at a certificate, maximum depth, minimum normalized width,
   cancellation, or node budget.
6. Revalidate the rectangular union before merging adjacent regions.
7. Build deterministic overlap/touch adjacency and connected components.
8. Bind certificates and stable region IDs to the resulting Atlas.

The update flow compares stable obstacle IDs, checks each region's stored
envelope, inherits or revalidates its exact subject, locally partitions
invalidated domains, rebuilds the graph, and binds the result to an immutable
parent/version transition. Removed or moved obstacles also trigger retries of
persisted unresolved domains.

HiPaC is a separate construction flow: validate a candidate polyline,
recursively certify or split segment OBBs, grow successful cells within a hard
validation budget, add portals only at shared certified witnesses, then label
components and bind subject digests.

## Design constraints

- Public headers do not expose Eigen, JSON, pybind11, or binary storage types.
- Samples influence exploration only and cannot raise evidence level.
- All expected failures return `Result<T>` in C++.
- No C++ struct is written directly to disk.
- The current validator is deliberately singular; adding another backend
  requires an explicit certificate and compatibility design.
- Atlas schema 2 and corridor schema 1 are independent. OBB and portal records
  are never injected into the Atlas binary files.
- Region-database schema 1 is a third independent format and imports producers
  only through explicit validated conversion.
- Planning roadmaps and optimization programs are derived, in-memory consumer
  artifacts in v0.8; neither introduces a storage schema or raises evidence.
- Shield decisions and telemetry are derived, in-memory v0.9 artifacts. They
  do not alter any persisted schema or promote geometric evidence to an
  execution guarantee.
- Policy proposals, decisions, and feedback are v2.0 application-facing
  artifacts. Only the feedback database is persistent, under its independent
  schema 1; its labels remain below execution evidence.
- Safety-memory records are v3.0 identity/lifecycle metadata. Direct reuse
  requires exact deployment, robot, and scene identity. Fleet reports reason
  only about declared conservative workspace envelopes and logical time
  windows; they never raise `EvidenceLevel`.
- Safety-memory-store schema 1 is a v3.1 immutable wrapper. It versions whole
  memory states without changing safety-memory schema 1 or merging concurrent
  histories.
- The major-version API-surface snapshot is a source-review gate, not a binary ABI
  description. The release benchmark consumes public APIs and deterministic
  synthetic fixtures; timing and memory estimates are diagnostic and are not
  part of any certificate.
