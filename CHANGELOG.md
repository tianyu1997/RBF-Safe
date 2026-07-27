# Changelog

All notable changes are documented here. The project follows Semantic
Versioning for library releases and versions its on-disk schemas separately.

## [3.12.0] - 2026-07-27

### Added

- Public revocation-aware `ExecutionLedger` under `RBFSafe::execution`, with a
  deterministic append-only record chain, cross-process writer exclusion, and
  optimistic expected-head mutation for one exact bounded session.
- Strict authorization/completion ordering: commands cannot be duplicated,
  skipped, or overlapped, and every next command requires an exact
  controller-endpoint Ed25519 completion statement for the previous
  authorization.
- Current signed-checkpoint revalidation before each command, rollback/fork
  rejection, and terminal automatic revocation when an original reviewer key
  is missing, inactive, out of sequence, or no longer publication-capable.
- Explicit terminal cancellation, closed-session expiration, and exact
  profile/Atlas/scene/controller/monitor/reviewer/checkpoint revocation
  records, plus failed/rejected controller outcomes.
- Bounded schema-1 directory persistence, complete offline audit, C++/Python
  APIs, native/Python inspection, deterministic quickstarts, corruption and
  concurrency regressions, and a fixed public cross-platform fixture.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.12.0. Existing session and earlier 3.x storage schemas remain
  readable and unchanged.
- The release benchmark now completes an ordered two-command ledger per named
  case and binds only its discrete record, completion, and checkpoint counts
  into the cross-platform logical digest.
- Ledger state and audit remain `Unknown` and never authorize execution. Only
  one exact returned command authorization is `RuntimeExecutable`; no hardware
  I/O, clock reads, physical attestation, tracking proof, or execution claim
  was added.

## [3.11.0] - 2026-07-27

### Added

- Public `RBFSafe::execution` target with deterministic command sequences
  bound to an exact Atlas, continuous trajectory audit, region sequence, and
  connectivity certificate.
- Bounded execution requests tied to one reviewed deployment profile and
  exact trust root/checkpoint/head, explicit controller and runtime-monitor
  Ed25519 endpoint keys, fresh nonce, and command/start/duration limits.
- Role-aware execution-session approvals by the exact deployment reviewers,
  separate signed controller acceptance, and independently signed armed
  runtime observations with caller-supplied monotonic time.
- Exact per-command authorization that returns `RuntimeExecutable` only for
  the signed index/configuration inside one closed latency/session window.
  Sessions themselves remain `Unknown` and never authorize execution.
- Bounded atomic schema-1 persistence with complete trust/profile/Atlas
  replay, C++/Python APIs, CLI verification and exact-command inspection,
  runnable quickstart, and a fixed public cross-platform fixture.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.11.0. Existing 3.x targets and storage readers remain
  available and unchanged.
- The release benchmark now verifies one complete bounded session and exact
  command authorization per named case and binds only discrete outcomes and
  counts into the cross-platform logical digest.

## [3.10.0] - 2026-07-27

### Added

- Public `RBFSafe::deployment` target with deterministic deployment manifests
  binding exact robot, controller, platform, runtime-build, trust-root,
  signed-checkpoint, and trust-head identities.
- Ed25519 deployment-profile approvals by active publication-capable keys,
  canonical approval sets, minimum/distinct-service quorum, and required
  Safety/Controls/Operations/Security reviewer roles.
- Reviewed runtime constraints for observation age, command latency, control
  period, missed cycles, monitor presence, fail-closed transport, and
  authenticated artifacts, with deterministic conformant/nonconformant
  assessments.
- Bounded atomic schema-1 persistence, C++/Python APIs and quickstarts,
  native/Python inspection, fixed cross-platform fixtures, and corruption,
  tamper, wrong-anchor, wrong-key, quorum, role, and resource-limit tests.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.10.0. All deployment-profile review and assessment output is
  governance metadata with `Unknown` evidence and never authorizes actuation.
- Existing trust-bundle, trust-history, checkpoint, Atlas, and other storage
  schemas are unchanged and retain their exact identities.
- The release logical digest now covers exact-root/checkpoint/head deployment
  profiles, two-role review quorums, and conformant but non-executable runtime
  assessments.

## [3.9.0] - 2026-07-27

### Added

- Explicit schema-3 trust-bundle rotation policy with an immutable signature
  threshold and optional distinct-service quorum.
- Canonical multi-signer successor authorization sets and schema-2 trust
  histories that replay them without changing schema-1 single-signer history.
- Exportable Ed25519-signed trust checkpoints, bounded standalone persistence,
  caller-pinned checkpoint verification, and C++/Python/native inspection.
- Cross-language quickstarts, fixed schema-3 bundle/schema-2 history/schema-1
  checkpoint fixtures, and quorum, tamper, rollback, duplicate-signer, wrong-key,
  and resource-limit regressions.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.9.0. Bundle schemas 1/2 and history schema 1 remain readable and
  are never implicitly upgraded to quorum policy.
- The release benchmark now exercises a 2-of-2 rotation and signed checkpoint
  and binds authorization, checkpoint-signature, and checkpoint counts into its
  logical digest.

## [3.8.0] - 2026-07-27

### Added

- Ed25519-signed trust-bundle successor authorizations bound to the exact
  predecessor, successor, sequences, signer service, and rotation-capable key.
- `ServiceTrustHistory`, an immutable schema-1 directory with caller-pinned
  root and expected-head checks, cross-process writer exclusion, signed
  rotation records, bounded replay, cancellation, and historical bundle
  lookup.
- C++/Python APIs, native/Python inspection, quickstarts, corruption and
  rollback regressions, and fixed schema-2 bundle/schema-1 history fixtures.

### Changed

- New service trust bundles use schema 2 and add an immutable `allow_rotate`
  key permission. Schema-1 bundles remain readable with rotation permission
  disabled and cannot silently become signed-history roots.
- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.8.0. Existing 3.x APIs and storage readers remain available.
- The release benchmark now signs, publishes, and replays a trust successor
  and binds the discrete authorization/history counts into its logical digest.

## [3.7.0] - 2026-07-27

### Added

- Public `RBFSafe::identity` target with RFC 8032 Ed25519 primitives,
  deterministic service public-key IDs, caller-pinned public trust bundles,
  operation/sequence constraints, and pending/active/retired/revoked policy.
- Parent-linked monotonic bundle rotation, bounded atomic schema-1 bundle
  persistence, fixed public fixture, C++/Python/native inspection, and
  reproducible quickstarts. Private keys are never persisted.
- Offline fetch/publication verification that preserves the complete 3.6
  memory/lifecycle/request/response/byte contract and records the exact
  verification-key and trust-bundle IDs in schema-2 transfer journals.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.7.0. Existing 3.x APIs and storage readers remain available.
- Artifact-transfer-journal writers now emit schema 2. Readers retain schema 1
  support, and legacy HMAC/unauthenticated transfer identities are unchanged.
- The release benchmark now exercises caller-pinned Ed25519 verification and
  binds its discrete key/transfer counts into the cross-platform digest.
- Monocypher 4.0.2 is vendored from its official archive under its BSD
  2-Clause option; exact upstream hashes and third-party notices are included.

## [3.6.0] - 2026-07-27

### Added

- Public `RBFSafe::remote` target with deterministic transport-neutral artifact
  fetch and publish contracts bound to exact safety-memory identity, artifact
  generation/state, service, locator, media type, payload length, and SHA-256.
- Request/response-bound HMAC-SHA256 service attestations that prevent an
  ordinary payload proof from being replayed as acknowledgement of a different
  remote operation; explicit unauthenticated mode remains visibly weaker.
- Verified transfer records and expected-head append-only journals with
  bounded checksummed schema-1 persistence, fixed fixtures, C++/Python APIs,
  native/Python inspection, and runnable examples.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.6.0. Existing 3.x APIs and storage readers remain available.
- Remote-transfer-compatible memory artifacts must use the exact payload-byte
  SHA-256 as `content_digest`; the general memory catalog retains its prior
  opaque-digest semantics outside this opt-in API.

## [3.5.0] - 2026-07-27

### Added

- Deterministic operational calibration windows with total-variation,
  calibration-error, overall success-drop, and per-bin success-drop
  measurements plus explicit insufficient/stable/drift outcomes.
- Parent-linked calibration lifecycle history with optimistic head checks,
  fail-closed automatic quarantine/pending-review transitions, explicit
  reviewed activation, bounded schema-1 persistence, and replay validation.
- Lifecycle-guarded calibrated policy gating, C++/Python APIs, CLI/native
  inspection, runnable examples, fixed fixtures, and post-deployment
  monitoring safety guidance.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.5.0. Existing 3.x APIs and storage readers remain available.
- The release benchmark now exercises an active, stable calibration lifecycle
  and binds its discrete stable/active outcomes into the logical digest while
  excluding all transitive floating-point-derived identities.

## [3.4.0] - 2026-07-23

### Added

- Deterministic policy-calibration profiles binding exact policy-model,
  deployment-scope, task, dataset, method, outcome, uncertainty-unit, and
  reliability-bin metadata.
- Recomputed ECE, maximum bin error, and 95% Wilson lower bounds; a calibrated
  policy gate that never upgrades raw confidence and still delegates every
  eligible action to the geometric shield.
- Bounded schema-1 profile persistence, fixed fixtures, C++/Python APIs,
  CLI/native inspection, runnable examples, and explicit measurement/drift
  safety guidance.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.4.0. Existing 3.x APIs and storage readers remain available.
- The public `RBFSafe::policy` target now also exposes the calibration layer;
  calibrated outputs remain below `RuntimeExecutable` evidence.

## [3.3.0] - 2026-07-23

### Added

- Public `RBFSafe::trust` target with HMAC-SHA256 artifact attestations bound
  to exact `SafetyMemory` artifact identity, lifecycle state, logical content,
  payload bytes, service/key identity, media type, and sequence.
- Bounded schema-1 attestation sidecars, atomic save, constant-time tag
  comparison, RFC 4231 conformance coverage, C++/Python APIs, CLI/native
  inspection, fixed fixtures, and runnable examples.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.3.0. Existing 3.x APIs and storage readers remain available.
- Installed fixtures and public API review now include artifact attestations
  and the `RBFSafe::trust` target.

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
