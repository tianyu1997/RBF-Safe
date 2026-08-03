# Changelog

[简体中文](CHANGELOG.zh-CN.md) | English

All notable changes are documented here. The project follows Semantic
Versioning for library releases and versions its on-disk schemas separately.

## [Unreleased]

### Documentation

- Add a curated Simplified Chinese documentation set for the most important
  user, safety, integration, and maintainer guides, with bilingual navigation
  and a CI coverage/link checker. Detailed English specifications remain the
  authoritative reference.
- Add a categorized English documentation index, simplify the root README,
  unify Markdown link/index validation, and ignore recurring local build,
  install, wheel-test, package-cache, and temporary outputs.

## [4.7.0] - 2026-08-01

### Added

- Public analytic `GeometricJacobian` and
  `SerialRobotModel::end_effector_geometric_jacobian` for modified-DH serial
  robots. The deterministic 6-by-N row-major result is expressed in the
  workspace frame, supports revolute and prismatic joints plus fixed tool
  frames, uses only standard-library public types, and mirrors into Python.
- Analytic planar and prismatic regressions, central finite-difference
  translation checks, error-path tests, and independent downstream CMake
  consumption of the new public type.
- A machine-readable `project.md` scope manifest, maintained completion
  matrix, and CI traceability gate. Eighteen product requirements now each
  retain public-API, behavioral-test, and documentation evidence.
- A kinematics guide documenting the modified-DH convention, geometric
  Jacobian rows and columns, error behavior, and its relationship to
  IFK-AA/LinkIAABB certification.

### Compatibility and safety

- All 4.6 APIs and storage schemas remain supported. The Jacobian is an
  additive point-kinematics API and no persisted identity or format changes.
- A Jacobian is deterministic numerical geometry, not collision evidence,
  region/corridor certification, or permission to execute. Existing
  certificate, audit, and exact runtime checks remain mandatory for their
  respective claims.
- The completion matrix proves repository traceability for the original
  software plan; it does not certify hardware, perception, clocks, transport,
  controller admission, or physical execution.

## [4.6.0] - 2026-07-29

### Added

- Deterministic `CoordinatedReservationAgreement` assembly and replay across
  independent trust-rotating occupancy histories. Every unique participant
  must authenticate the exact same separated continuous-fleet payload at one
  evaluation tick.
- Canonical order-independent membership, exact deployment/occupancy,
  publisher/stream/key/sequence, trust/publication prefix,
  verified-publication, timeline/frame, payload, separation-report, common
  validity-window, protocol/round/parent binding, and stable identities.
- Strict successor verification with stable membership/protocol, exact parent
  linkage, increasing rounds and participant publication sequences, plus
  replay of older agreements against later-extended histories.
- Bounded checksummed schema-1 persistence, atomic non-overwriting
  publication, corruption and symlink rejection, C++/Python APIs, native and
  Python inspection, dual-language quickstarts, and a fixed two-deployment
  cross-platform fixture.

### Compatibility and safety

- All 4.5 APIs and storage schemas remain supported. Coordinated agreements
  are independent artifacts; existing publications and histories are never
  enrolled, merged, or upgraded implicitly.
- An agreement authenticates unanimous retained software statements. It does
  not prove global freshness, participant receipt, network consensus, clock
  synchronization, controller admission, reservation enforcement, or
  physical execution. Callers retain newest trust/publication heads and the
  accepted agreement ID outside the stored artifacts' rollback domain.
- Agreements and participants remain `Unknown` and non-authorizing, including
  agreements built over a
  `CertifiedSeparatedUnderSweptEnvelopes` occupancy result.

## [4.5.0] - 2026-07-28

### Added

- Independent `RotatingOccupancyPublicationHistory` with an embedded,
  replayable `ServiceTrustHistory`; single-signature and canonical quorum
  trust rotation; historical-bundle publication verification; monotonic
  trust use; and publication appends restricted to the exact current trust
  head.
- Caller-pinned trust-root/head and publication-root/head opening plus a
  signed-checkpoint trust-anchor overload. Deterministic dual-chain audit
  reports independent trust and publication relations, common prefixes, and
  forks.
- Bounded schema-1 directory persistence with atomic creation,
  cross-process expected-head publication, exact orphan recovery, aggregate
  payload limits, cancellation, corruption and symlink rejection,
  C++/Python APIs, native/Python inspection, dual-language quickstarts, and a
  fixed two-key/two-publication fixture.

### Compatibility and safety

- All 4.4 APIs and storage schemas remain supported.
  `OccupancyPublicationHistory` schema 1 remains fixed-trust; rotation uses
  the new independent format and is never inferred or applied implicitly.
- Authorized rotation authenticates retained software statements but does not
  make a head globally newest. Callers must retain the newest accepted trust
  head or checkpoint and publication head outside the directory's rollback
  domain.
- Rotating histories, records, verified publications, and dual-chain audits
  remain `Unknown` and non-authorizing. Transport, peer discovery, gossip,
  consensus, trustworthy clocks, perception, controller I/O, and physical
  execution remain external.

## [4.4.0] - 2026-07-28

### Added

- Deterministic `MovingObstacleOccupancy` records for timestamped
  piecewise-linear workspace AABB trajectories, with outward-rounded swept
  unions, optional obstacle padding, stable slice identities, replay
  verification, resource limits, and cancellation.
- `analyze_continuous_robot_scene_occupancy` with exact timeline, workspace
  frame, and complete-window matching; unique robot/obstacle identities;
  conservative robot-link versus obstacle-slice overlap and separation-margin
  witnesses; canonical ordering; and bounded work.
- Checksummed schema-1 `ContinuousRobotSceneOccupancyBundle` persistence with
  semantic analysis replay, atomic non-overwriting publication, symlink and
  corruption rejection, byte/path loaders, C++/Python APIs, native/Python
  inspection, quickstarts, and a fixed cross-platform fixture.

### Compatibility and safety

- All 4.3 APIs and every prior storage schema remain supported. The new bundle
  format is independent; static `SceneSnapshot` obstacles, robot fleet
  bundles, and authenticated occupancy histories are never upgraded or mixed
  implicitly.
- Obstacle waypoints are caller-supplied deterministic bounds, not trusted
  perception or prediction. Exact clock semantics, interpolation validity,
  localization, tracking uncertainty beyond explicit padding, self/static
  collision freedom, dynamics, control, and hardware enforcement remain
  external.
- Moving-obstacle occupancies, reports, conflicts, and bundles remain
  `Unknown` and non-authorizing, including the
  `CertifiedSeparatedUnderSweptEnvelopes` status.

## [4.3.0] - 2026-07-28

### Added

- `OccupancyPublicationHistory` with caller-pinned stream, publisher, exact
  trust snapshot, root, and current head; immutable records; exact stored
  occupancy bytes; independent signature/payload replay; and lookup/head
  verification.
- Expected-head optimistic concurrency, cross-process writer exclusion,
  append commit records, bounded counts/metadata/total payloads, cancellation,
  symlink rejection, corruption checks, and schema-1 directory persistence.
- Deterministic two-history audits reporting identical, first-extends-second,
  second-extends-first, or forked, with common-prefix evidence and a stable
  audit ID.
- C++/Python APIs and quickstarts, native/Python inspection and comparison,
  downstream package coverage, release-benchmark replay, and a fixed
  cross-platform two-publication fixture.

### Compatibility and safety

- All 4.2 APIs and occupancy-publication schema-1 files remain supported.
  History schema 1 stores one exact trust bundle; trust rotation starts a
  separate history.
- A caller-retained expected head detects local rollback. Fork detection
  requires two independently observed histories and does not select a globally
  authoritative branch.
- Histories, records, verifications, and fork audits remain `Unknown` and
  non-authorizing. Transport, replication, consensus, trustworthy clocks,
  collision certification, controller I/O, and execution authority remain
  outside this release.

## [4.2.0] - 2026-07-28

### Added

- Public `RBFSafe::coordination` target with deterministic
  `OccupancyPublication` and `VerifiedOccupancyPublication` values.
- Exact-byte continuous-fleet-occupancy binding, Ed25519 publication under an
  active publish-authorized service key, exact trust-bundle binding, closed
  logical-tick validity windows, and explicit caller pins for stream,
  publisher, trust bundle, parent, and evaluation tick.
- Monotonic parent-linked publisher-stream succession checks, bounded
  checksummed schema-1 persistence, atomic non-overwriting publication,
  symbolic-link rejection, C++/Python APIs and quickstarts, native/Python CLI
  verification, and a fixed cross-platform fixture.
- Exact-byte occupancy loading so digest, decoded bundle identity, timeline,
  frame, and trajectory-window coverage are verified from one in-memory read.

### Compatibility and safety

- All documented 4.1 APIs and continuous-fleet-occupancy schema-1/schema-2
  bytes remain supported. The new publication format is independent and no
  unsigned occupancy bundle is upgraded implicitly.
- A verified publication authenticates a software statement relative to
  caller-retained pins. It is not a network protocol, consensus result,
  globally fresh head, trusted clock, collision certificate, or execution
  authorization.
- Publications and verification results remain `Unknown` and
  non-authorizing. The caller must authenticate each successor, retain the
  accepted parent externally, and provide trustworthy tick and trust-head
  inputs.

## [4.1.0] - 2026-07-28

### Added

- `DeploymentFrameBounds` and
  `build_robot_trajectory_occupancy_in_frame` with validated row-major
  right-handed rotations, nominal translations, axis-wise translation
  uncertainty, and arbitrary-axis angular uncertainty.
- Conservative outward-rounded AABB transformation and angular expansion,
  exact frame binding in slice/occupancy identities, robot replay, C++/Python
  APIs, CLI frame summaries, examples, and property/regression tests.
- Continuous-fleet-occupancy schema 2 with exact field sets, bounded loading,
  mixed schema-1/schema-2 support, a fixed cross-platform fixture, and
  documented migration rules.

### Compatibility and safety

- The v4.0 translation-only builder and schema-1 reader remain functional and
  reproduce the original fixed fixture identities.
- Frame bounds are static for one occupancy. Moving frames, time-varying
  localization, dynamic obstacles, clock guarantees, tracking, dynamics, and
  hardware enforcement remain external.
- Every occupancy and fleet result remains `Unknown` and non-authorizing.

## [4.0.0] - 2026-07-28

### Added

- Public `RBFSafe::occupancy` target with timestamped piecewise-linear joint
  trajectories, deterministic normalized-width subdivision, conservative
  IFK-AA per-link swept AABBs, fixed workspace translations, and exact
  robot/timeline/frame/deployment identity binding.
- Bounded deterministic multi-robot analysis with explicit
  `CertifiedSeparatedUnderSweptEnvelopes` and `PotentialConflict` outcomes,
  overlap/separation-margin link witnesses, canonical input/output ordering,
  resource budgets, and cancellation.
- Exact robot-model replay verification plus checksummed schema-1
  `ContinuousFleetOccupancyBundle` persistence with complete time-coverage
  checks, bounded loading, report replay, atomic publish, and overwrite/
  symbolic-link protections.
- C++ and Python high-level APIs, native and Python inspection, optional
  per-deployment robot replay and margin reanalysis, deterministic quickstart,
  and a fixed Linux/Windows-readable fixture.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 4.0.0. The full documented 3.x source surface and every prior
  storage schema remain available; the v4 public-surface snapshot records the
  new additive contract.
- Continuous occupancy results remain `Unknown` and never authorize
  execution. The layer proves only separation under exact conservative swept
  envelopes; obstacle/self-collision freedom, rotated or uncertain frames,
  clock synchronization, dynamics, tracking, transport, and hardware
  interlocks remain external.

## [3.15.0] - 2026-07-28

### Added

- Public `RBFSafe::provenance` target with explicitly scoped, adapter-
  normalized hardware-key attestation statements, active Ed25519 authority
  signatures, exact subject/trust binding, deterministic identities, and
  contiguous multi-attester provenance-chain replay.
- Caller-pinned hardware provenance policy for exact adapter
  ID/version/format, attestation authority, vendor, required usage, quorum,
  distinctness, and resource bounds. No vendor, adapter, or hardware root is
  accepted implicitly.
- Signed per-source external time assertions, contiguous monotonic source
  chains, explicit clock namespace, uncertainty intervals, source quorum, and
  overflow-safe `Fresh`, `Incomplete`, `Stale`, `Future`, and `Inconsistent`
  evaluation against a caller-supplied time.
- Checksummed schema-1 `VerifiableProvenanceBundle` persistence with atomic
  publish, overwrite protection, bounded/cancellable validation, C++/Python
  replay APIs, native/Python inspection, deterministic quickstart, and a
  fixed cross-platform fixture.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.15.0. All earlier 3.x storage schemas remain readable and
  unchanged.
- Provenance `SATISFIED`, time `FRESH`, and combined `ready` remain
  non-authorizing `Unknown` audit metadata. The new layer does not read a
  local clock during freshness evaluation, validate a vendor blob, prove
  physical key custody, or upgrade geometric/connectivity/execution evidence.
- The deterministic release benchmark now replays the fixed provenance
  fixture against exact trust pins and includes only its stable identities,
  discrete counts, and statuses in the cross-platform logical digest.

## [3.14.0] - 2026-07-27

### Added

- Public `RBFSafe::witness` target with independently signed transparency
  checkpoint cosignatures, configurable quorum policy, canonical witness
  ordering, exact log/checkpoint/root/trust-bundle binding, and signer
  separation.
- Compact append-only consistency proofs built from an old Merkle frontier and
  aligned appended subtrees. Proof size and verification work are logarithmic
  in the represented tree ranges and do not disclose every historical leaf.
- Authenticated checkpoint gossip with per-sender sequence/parent chains,
  proof-DAG audit, explicit `Consistent`, `Incomplete`, and `SplitView`
  outcomes, same-size equivocation detection, and invalid-proof conflicts.
- Bounded schema-1 append-only gossip archives with caller-pinned log identity,
  exact trust bundle and expected head, immutable atomic records,
  cross-process writer exclusion, corruption checks, C++/Python APIs, native
  and Python inspection, deterministic quickstart, and fixed fixture.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.14.0. Existing transparency logs and all earlier 3.x storage
  schemas remain readable and unchanged.
- The release benchmark now verifies compact consistency, witness quorum,
  authenticated gossip, expected-head archive reopen, and split-view audit.
  Only discrete counts and statuses enter the cross-platform logical digest.
- Witnesses and gossip remain software statements under a caller-pinned trust
  bundle. They are `Unknown`, never authorize execution, and do not claim
  transport security, trustworthy time, hardware-backed key provenance,
  physical observation, or distributed consensus.

## [3.13.0] - 2026-07-27

### Added

- Public `RBFSafe::transparency` target with deterministic reviewed-deployment
  anchors bound to the exact approved profile, trust root, signed checkpoint,
  current bundle, sequence, and replayed history head.
- Independently signed runtime observations bound to one outstanding
  ledger-authorized command, its closed monotonic window, exact runtime
  snapshot, configuration digest, and canonical Ed25519 source quorum.
- Deterministic append-only Merkle log with caller-pinned log identity,
  Ed25519-signed checkpoints, inclusion proofs, explicit prefix-consistency
  witnesses, expected-head publication, and cross-process writer exclusion.
- Bounded schema-1 directory persistence with atomic complete record files,
  strict replay/corruption checks, C++/Python APIs, native/Python inspection,
  deterministic quickstart, and a fixed cross-platform two-record fixture.

### Changed

- C++, Python, citation, MoveIt package, and downstream requirements advance
  together to 3.13.0. All prior 3.x storage schemas remain readable and
  unchanged.
- The release benchmark now publishes one deployment anchor and one
  two-source runtime observation per named case, then verifies inclusion,
  consistency, expected-head reopen, and audit. Only discrete counts enter the
  cross-platform logical digest.
- Anchors, observations, log records, proofs, checkpoints, and audits remain
  `Unknown` and never authorize execution. Network publication, checkpoint
  gossip, trustworthy clocks, physical sensing, hardware roots, and controller
  tracking remain external.

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
