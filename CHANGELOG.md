# Changelog

All notable changes are documented here. The project follows Semantic
Versioning for library releases and versions its on-disk schemas separately.

## [3.2.0] - 2026-07-23

### Added

- `FleetScheduleArchive` with deterministic version identities, exact
  `SafetyMemory` and fleet-snapshot binding, idempotent expected-head
  publication, historical reads, and report replay verification.
- Independent fleet-schedule-archive schema 1 with bounded loading, aggregate
  resource limits, checksums, semantic conflict validation, atomic save,
  fixed-format fixtures, C++/Python APIs, CLI/native inspection, and runnable
  examples.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.2.0. Existing 3.x APIs and all previously supported storage
  schemas remain readable.
- The release logical digest now covers deterministic fleet-schedule archive
  publication and replay, and installation exports the schema-1 archive
  fixture.

## [3.1.0] - 2026-07-23

### Added

- Deterministic `SafetyMemory::identity()` and immutable
  `SafetyMemoryRevisionInfo` records.
- `SafetyMemoryStore` with bounded open/load operations, historical revision
  reads, idempotent publication, explicit expected-head concurrency control,
  and a fail-closed cross-process writer lock.
- Independent safety-memory-store schema 1 using immutable revision
  directories and immutable commit files, with fixed-format fixtures,
  C++/Python APIs, CLI/native inspection, and runnable examples.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.1.0. All 3.0 APIs and safety-memory schema-1 bytes remain
  readable; the store is an additive wrapper with its own schema.
- The release logical digest now covers the deterministic safety-memory
  identity, and installation exports the fixed two-revision store fixture.

## [3.0.0] - 2026-07-22

### Added

- Public `RBFSafe::memory` target with deterministic safety-artifact
  registration, exact deployment/robot/scene identity binding, monotonic
  lifecycle states, optimistic generations, and replayable audit events.
- Cross-task reuse assessment that distinguishes direct reuse, required
  revalidation, and ineligible artifacts without promoting stored evidence.
- Scene-wide memory invalidation and explicit reuse recording for long-lived
  robot deployments.
- Fleet snapshots, source-artifact-bound workspace reservations, and bounded,
  cancellable multi-robot time-window conflict and separation analysis.
- Atomic checksummed safety-memory schema-1 persistence with bounded loading,
  deterministic-ID checks, full history replay, C++/Python APIs, inspection,
  examples, and focused corruption and coordination tests.

### Changed

- C++, Python, citation, optional MoveIt package, and downstream package
  requirements advance together to 3.0.0. The documented 2.0 surface remains
  available and the 3.x source-compatibility line adds `RBFSafe::memory`.
- `RBFSafe::rbfsafe` now aggregates `RBFSafe::memory`. All prior storage
  schemas retain their existing bytes and independent version numbers.

## [2.0.0] - 2026-07-22

### Added

- Public `RBFSafe::policy` learning-policy safety target with bounded
  confidence, state/action uncertainty, observation-age, and inference-latency
  gates above the runtime shield.
- Deterministic `InputOrder`, `HighestConfidence`, and `LowestUncertainty`
  selection that prefers shield acceptance over repair and fails closed when
  no proposal remains usable.
- Identity-bound proposal/decision IDs, one aligned feedback record per input,
  five explicit training labels, synchronized telemetry, cancellation, and
  duplicate/resource validation without `RuntimeExecutable` promotion.
- Queryable `PolicyFeedbackDatabase` with atomic checksummed schema-1
  persistence, bounded loading, corruption detection, C++/Python inspection,
  C++/Python examples, and focused regression tests.
- Major-version API-surface snapshot selection and v2 policy, format, safety,
  architecture, roadmap, migration, and provenance documentation.

### Changed

- C++, Python, citation, optional MoveIt package, and downstream package
  requirements advance together to 2.0.0. The complete documented 1.0 API is
  retained while the new 2.x source-compatibility line begins.
- `RBFSafe::rbfsafe` now aggregates `RBFSafe::policy`. Atlas schemas 1/2,
  LECT schema 1, corridor schema 1, region-database schema 1, and version-store
  schema 1 remain unchanged.

## [1.0.0] - 2026-07-22

### Added

- Reviewed public C++ headers, installed CMake targets, and high-level Python
  exports with a documented 1.x source-compatibility and deprecation policy.
- Normalized public API SHA-256 manifest plus a CI gate that requires explicit
  review of additions or changes.
- Schema support and migration matrix covering every 0.x storage format,
  including tested Atlas schema-1 to schema-2 full-revalidation migration.
- Deterministic public-API release benchmark with bounded smoke/soak tests,
  independent collision point checks, update/inheritance checks, runtime
  shield checks, diagnostic timing/memory reporting, and a logical digest.
- Reproducible IIWA14, UR5, Panda, and Franka robot fixtures paired with shelf,
  industrial-cell, clutter, and mobile-manipulation synthetic scenes.

### Changed

- C++, Python, citation, and optional MoveIt package versions advance together
  to 1.0.0. The MoveIt consumer now requires the 1.0 core package.
- Atlas schema 2, LECT schema 1, corridor schema 1, region-database schema 1,
  and version-store schema 1 remain unchanged and independently versioned.

## [0.9.0] - 2026-07-22

### Added

- Public `RBFSafe::shield` target for joint-delta, end-effector pose, and
  piecewise-linear trajectory actions with explicit deterministic `ACCEPT`,
  `REPAIR`, and `REJECT` decisions.
- Component-constrained bounded joint/trajectory repair, connected Safe IK
  translation, mandatory final continuous audit, stable decision IDs,
  cancellation, and waypoint/resource budgets.
- Ordered VLA proposal batches that select the first accepted candidate or
  first repairable fallback while retaining every per-action decision.
- Thread-safe aggregate telemetry and a stateful runtime monitor for on-plan,
  certified-deviation, uncertified-state, and inactive observations.
- Matching Python APIs, C++ quickstart, focused safety/property tests, and a
  runtime-shield integration and evidence guide.

### Changed

- `RBFSafe::rbfsafe` now aggregates `RBFSafe::shield`. Atlas schema 2,
  corridor schema 1, region-database schema 1, and version-store schema 1 are
  unchanged.
- The MoveIt package metadata now requires the matching 0.9 core release.

## [0.8.0] - 2026-07-22

### Added

- Public `RBFSafe::planning` target with deterministic certified-union
  sampling, bounded near sampling, certified roadmap centers, exact portal
  witnesses, single-region edge evidence, cancellation, and resource budgets.
- High-level OMPL RRT, RRT*, PRM, and BIT* helpers with native or Atlas-guided
  sampling, optional revalidated PRM roadmap seeding, explicit outcomes, and a
  mandatory final continuous trajectory audit for certified exact solutions.
- MoveIt 2 certified constraint-sampler allocator with identity-checked Atlas
  resources, configurable sampling policy, and optional certified-roadmap bias.
- Public `RBFSafe::optimization` target with solver-neutral AABB, OBB, Portal,
  zonotope, and Taylor linear constraints; deterministic trajectory assignment;
  residuals; bounded projection; and TrajOpt, CHOMP, STOMP, and MPC front ends.
- Matching Python APIs, C++ quickstarts, framework integration tests, and
  planning/optimization safety and integration documentation.

### Changed

- `RBFSafe::rbfsafe` now aggregates the planning and optimization targets.
  Atlas schema 2, corridor schema 1, and region-database schema 1 are unchanged.
- The optional OMPL component now depends on `RBFSafe::planning` and supports
  planner construction and solve orchestration in addition to low-level hooks.

## [0.7.0] - 2026-07-22

### Added

- Public `RBFSafe::regions` target and unified `RegionDatabase` records for
  AABB, OBB, convex Portal, TrajectoryTube, zonotope, and first-order Taylor
  geometry with deterministic IDs and certificate lookup.
- Arbitrary AABB/OBB pair Portal discovery using conservative AABB pruning and
  deterministic half-space feasibility, rather than only consecutive path
  cells.
- Seed-driven OBB Atlas growth with point cells, nearest-neighbor segment
  tubes, bounded growth/validation/pair budgets, cancellation, deterministic
  ordering, and connected-component construction.
- Correlation-preserving first-order Taylor IFK for zonotope/Taylor regions,
  conservative nonlinear remainders, higher-order certificate issuance, and
  containment property tests.
- Checksummed, atomic region-database schema 1 persistence and matching C++,
  Python, and `rbfsafe-inspect` load/query support.

### Changed

- `RBFSafe::rbfsafe` now aggregates `RBFSafe::regions`; Atlas schema 2 and
  corridor schema 1 remain unchanged and independently versioned.

## [0.6.0] - 2026-07-22

### Added

- Public `RBFSafe::update` target with deterministic obstacle-ID
  `SceneDelta`, conservative region invalidation, bounded local LECT repair,
  cooperative cancellation, resource budgets, and C++/Python APIs.
- Envelope-backed certificate inheritance with exact regional subject,
  parent-certificate, transition, policy, scene, and clearance binding.
- Persisted unresolved repair domains so coverage can recover when obstacles
  move away or are removed.
- Immutable `AtlasVersionStore` publication, historical loading, parent-chain
  validation, branching rollback, and Python/C++ inspection support.
- Dynamic-update quickstart, CLI update/store operations, schema-1 fixture,
  transition corruption tests, and cross-version migration tests.

### Changed

- New Atlas builds use schema 2 with `dependencies.bin`, optional
  `transition.json`, Atlas version metadata, certificate lineage, and exact
  region subject digests.
- The Atlas reader accepts both schemas 1 and 2. Updating schema-1 data forces
  direct revalidation before writing schema 2.
- The aggregate target now includes `RBFSafe::update`.

## [0.5.0] - 2026-07-22

### Added

- Public `Pose3d` and deterministic end-effector pose computation with a
  normalized `x,y,z,w` quaternion.
- Public `RBFSafe::ik` target with bounded projected damped-least-squares Safe
  IK, explicit evidence separation, cancellation, statistics, and C++/Python
  APIs.
- Deterministic `SafeAtlas::route` recovery through exactly intersecting AABB
  regions with subject-bound `CertifiedConnectivity` certificates.
- Safe IK quickstart and `rbfsafe-inspect` pose/seed query support.
- Optional ROS 2 Jazzy `rbfsafe_moveit` ament package with fail-closed start
  state, final trajectory, and connected Safe IK plugins.
- MoveIt plugin discovery plus functional Safe IK/request/response gate tests,
  and deployment/configuration guides.

### Changed

- `SafeAtlas::connected` now delegates to exact route recovery; adjacency
  tolerance cannot by itself certify connectivity across a geometric gap.
- `RBFSafe::rbfsafe` aggregates the Safe IK target while core and Python wheel
  builds remain independent of ROS and MoveIt.

## [0.4.0] - 2026-07-22

### Added

- Public `RBFSafe::corridor` target with standard-library `CspaceObb` values,
  deterministic segment-tube generation, conservative OBB validation, and a
  standalone bounded `ObbGrower`.
- Bounded lateral OBB growth and recursive HiPaC path covering with
  `CERTIFIED`, `PARTIAL`, and `INVALID` reports.
- Subject-bound certified OBB cells, shared-witness portals, deterministic
  connected components, and `CertifiedConnectivity` route certificates.
- Checksummed, atomic corridor schema 1 persistence plus matching C++ and
  Python save/load/query APIs.
- OBB enclosure property tests, independent collision regression sampling,
  route-convexity tests, corruption tests, and a HiPaC quickstart.

### Changed

- `RBFSafe::rbfsafe` now aggregates the corridor layer in addition to Atlas.
- `Certificate` exposes an optional `subject_digest`; existing Atlas schema-1
  certificates retain their v0.1-v0.3 identity and storage representation.

## [0.3.0] - 2026-07-22

### Added

- Optional `RBFSafe::ompl` CMake component for `RealVectorStateSpace` planning.
- Certified-only OMPL state validity, continuous motion validation backed by
  the trajectory auditor, and certified-region state sampling.
- Adapter statistics, deterministic single-thread sampling seeds, an
  RRTConnect example, and installed-component consumer tests.
- A deterministic immutable region BVH for Atlas membership and nearest-region
  queries, rebuilt in memory without changing schema 1.

### Changed

- C++ and Python package versions advance together while the Python wheels
  remain independent of OMPL.

## [0.2.0] - 2026-07-21

### Added

- C++ and Python `TrajectoryAuditor` APIs with deterministic continuous
  line-segment coverage against certified Atlas regions.
- `CERTIFIED`, `PARTIAL`, and `INVALID` reports with parameter-space coverage,
  region sequences, explicit uncovered intervals, resource budgets, and
  cooperative cancellation.
- `rbfsafe-inspect --trajectory` support for JSON waypoint arrays.

### Changed

- CI uses explicit Release configurations for single- and multi-config CMake
  generators and current Node 24 GitHub actions.

## [0.1.0] - 2026-07-21

### Added

- C++20 geometry, LECT, Atlas, and aggregate CMake targets.
- Serial DH robot and AABB scene models with deterministic SHA-256 identity.
- IFK-AA + LinkIAABB conservative regional certification.
- Deterministic seed-guided Atlas construction, merging, adjacency, and
  connected-component queries.
- Public mutable LECT and immutable snapshot APIs with stable path keys.
- Checksummed little-endian Atlas schema v1 with atomic publication.
- High-level `rbfsafe` Python bindings, specific exception types, CLI, and
  optional 2-D visualization.
- Windows/Linux CI, sanitizer jobs, installed-wheel tests, downstream CMake
  consumption tests, and legacy 2-DOF/IIWA14/UR5 golden fixtures.
