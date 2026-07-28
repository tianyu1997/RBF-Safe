# Roadmap

## Implemented releases

- **v0.1:** DH+AABB geometry, IFK-AA/LinkIAABB certificates, public LECT,
  SafeAtlas build/query/persistence, C++ and Python packages.
- **v0.2:** continuous trajectory auditor with `CERTIFIED`, `PARTIAL`, and
  `INVALID` reports, coverage ratio, deterministic region sequence, and
  explicit uncovered intervals.
- **v0.3:** optional OMPL component with certified-only state validity,
  continuous motion validation, and certified-region sampling.
- **v0.4:** conservative OBB segment tubes, bounded growth, shared-witness
  portals, HiPaC recursive covering, and subject-bound connectivity routes.
- **v0.5:** deterministic region-constrained Safe IK, explicit Atlas routes,
  and fail-closed ROS 2 Jazzy / MoveIt 2 request, response, and kinematics
  plugins.
- **v0.6:** obstacle-ID scene differences, envelope-backed certificate
  inheritance, conservative invalidation, bounded local repair, coverage
  recovery, Atlas schema 2, and immutable version stores with rollback.
- **v0.7:** unified AABB/OBB/Portal/TrajectoryTube/zonotope/Taylor certificate
  records, arbitrary AABB/OBB Portal discovery, deterministic OBB Atlas
  growth, correlation-preserving first-order Taylor IFK, schema-1
  region-database persistence, and C++/Python/CLI queries.
- **v0.8:** deterministic certified-union sampling and roadmap
  construction, Atlas-guided OMPL RRT/RRT*/PRM/BIT* helpers with final path
  audit, MoveIt certified constraint sampling with optional roadmap bias, and
  solver-neutral region constraints with TrajOpt, CHOMP, STOMP, and MPC front
  ends.
- **v0.9:** joint, end-effector, and trajectory action checks with
  deterministic `ACCEPT`, `REPAIR`, and `REJECT`; component-constrained
  bounded repair; ordered VLA proposal batches; synchronized telemetry; and a
  runtime state monitor. All evidence remains below `RuntimeExecutable`.
- **v1.0 - Stable geometric safety library:** reviewed public source
  API with a 1.x compatibility policy and automated surface gate; documented
  schema migration matrix; deterministic public-API benchmark and bounded soak
  gates; and reproducible IIWA, UR5, Panda/Franka, shelf, clutter,
  industrial-cell, and mobile-manipulation regression fixtures.
- **v2.0 - Learning-policy safety:** uncertainty-, freshness-, and
  latency-aware proposal gating above the runtime shield; deterministic
  accept/repair selection; identity-bound aligned training feedback; bounded
  queryable schema-1 feedback persistence; C++/Python APIs, inspection tools,
  examples, telemetry, and fail-closed evidence rules.
- **v3.0 - Robot safety memory:** deterministic persistent artifact
  catalog, monotonic lifecycle and replayable audit log, exact-identity
  cross-task reuse, scene-wide invalidation, multi-robot fleet snapshots, and
  conservative time-window workspace-reservation analysis. The independent
  schema-1 memory format, C++/Python/CLI interfaces, resource and cancellation
  gates, and industrial deployment guidance are included.
- **v3.1 - Transactional memory history:** deterministic whole-memory
  identities, immutable revision/commit chains, historical reads,
  expected-head optimistic concurrency, cross-process writer exclusion,
  bounded loading, fixed-format fixtures, and C++/Python/CLI tooling.
- **v3.2 - Versioned fleet schedules:** deterministic linear schedule
  histories bound to exact fleet snapshots and safety-memory revisions,
  expected-head publication, replay verification, bounded schema-1 storage,
  fixed fixtures, C++/Python APIs, and inspection tooling.
- **v3.3 - Authenticated artifact bytes:** caller-keyed HMAC-SHA256
  attestations bound to memory lifecycle and exact payload bytes, bounded
  schema-1 sidecars, constant-time verification, fixed fixtures, C++/Python
  APIs, and inspection tooling. Secret management and transport remain external.
- **v3.4 - Calibrated policy metadata:** exact model/scope/task/data
  identity, deterministic reliability bins, recomputed ECE and maximum error,
  conservative 95% Wilson lower-bound confidence, quality gates, bounded
  schema-1 persistence, and C++/Python/inspection tooling. Statistical
  calibration remains below geometric certificate and execution evidence.
- **v3.5 - Calibration drift lifecycle:** bounded operational
  confidence/outcome windows; total-variation, calibration-error, and
  success-drop gates; explicit insufficient/stable/drift reports;
  parent-linked optimistic-concurrency history; automatic fail-closed
  quarantine/pending-review transitions; reviewed activation; replayable
  schema-1 persistence; and exact-head guarded policy gating.
- **v3.6 - Remote artifact service contract:** deterministic,
  transport-neutral fetch/publication requests; exact whole-memory, lifecycle,
  byte-length and SHA-256 binding; request/response-bound HMAC service
  attestations; explicit unauthenticated mode; verified-transfer metadata;
  expected-head audit journals; bounded schema-1 persistence; and
  C++/Python/inspection tooling.
- **v3.7 - Public-key service identities:** RFC 8032 Ed25519
  service signing; caller-pinned deterministic public trust bundles;
  pending/active/retired/revoked key policy; operation and service-sequence
  constraints; monotonic parent-linked rotation; offline fetch/publication
  verification; schema-2 transfer journals with public provenance; bounded
  schema-1 bundle persistence; fixed fixtures; and C++/Python/inspection
  tooling. HMAC and unauthenticated 3.6 identities remain compatible.
- **v3.8 - Authorized trust rotation:** exact-successor Ed25519
  authorization by explicitly rotation-capable active keys; schema-2 public
  trust bundles; immutable schema-1 local trust histories; caller-pinned root
  and caller-retained expected-head checks; cross-process publication;
  bounded deterministic replay; historical bundle lookup; fixed fixtures; and
  C++/Python/inspection tooling. A local history alone cannot detect rollback
  of its complete directory without the external expected-head anchor.
- **v3.9 - Quorum trust and portable checkpoints:** schema-3 immutable
  minimum-signature/distinct-service bundle policy; canonical authorization
  sets; schema-2 replayable histories; signed schema-1 head checkpoints;
  root/checkpoint caller pins; bounded C++/Python persistence; CLI
  verification; fixed cross-platform fixtures; and deterministic release
  gates.

## Continued product hardening

- **v3.10 - Reviewed deployment profiles:** deterministic schema-1
  manifests binding exact robot/controller/platform/runtime and trust
  checkpoint identities; timing/monitor/transport/artifact constraints;
  role-aware Ed25519 approval quorums; bounded C++/Python persistence;
  conformant/nonconformant assessments; CLI verification; and fixed
  cross-platform fixtures. All output remains `Unknown` evidence and cannot
  authorize actuation.
- **v3.11 - Bounded execution sessions:** exact reviewed-profile,
  trust-checkpoint/head, Atlas, re-audited trajectory/route, command schedule,
  reviewer, controller, and independent runtime-monitor binding; bounded
  schema-1 persistence; C++/Python/CLI tooling; fixed fixtures; and
  `RuntimeExecutable` evidence only for one exact command/configuration inside
  one closed monotonic window. The session itself remains `Unknown` and is
  never an open-ended permit.
- **v3.12 - Revocation-aware execution ledger:** append-only,
  expected-head session/command-authorization history; current-checkpoint
  revalidation before each authorization; explicit expiration, cancellation,
  and key/profile/scene revocation records; independently attested controller
  completion observations; and offline audit without adding hardware I/O.
- **v3.13 - Deployment and runtime transparency:** exact
  reviewed-profile/trust-history deployment anchors; independent
  source-signed observations bound to one outstanding ledger command;
  deterministic Merkle leaves; signed checkpoints; inclusion and explicit
  prefix-consistency witnesses; expected-head, bounded schema-1 persistence;
  fixed fixtures; and C++/Python/inspection tooling. Every transparency value
  remains `Unknown` and non-authorizing.
- **v3.14 - Witnessed checkpoint distribution:** compact
  append-only frontier/subtree consistency proofs, independently cosigned
  checkpoint quorums, authenticated sender chains, proof-DAG audit,
  same-size-equivocation and invalid-proof split-view detection, bounded
  append-only schema-1 archives, fixed fixtures, and C++/Python/inspection
  tooling.
- **v3.15 - Verifiable provenance and time inputs:** pluggable,
  explicitly scoped hardware-key attestation statements, signed external time
  assertions, freshness policy, provenance-chain replay, and adapters that
  remain separate from geometric and physical-execution certification. No
  device vendor or time source is trusted implicitly.
- **v4.0 (current) - Continuous-time fleet occupancy:** deterministic
  piecewise-linear joint-trajectory subdivision, conservative IFK-AA per-link
  swept AABBs, explicit timeline/frame/deployment binding, translated
  multi-robot comparison, separation-margin witnesses, replay verification,
  bounded schema-1 persistence, and C++/Python/inspection tooling. Every
  result remains `Unknown` and non-authorizing.
- **v4.1+ candidates:** rotated/uncertain deployment frames, distributed
  fleet coordination, authenticated occupancy publication, continuous moving
  obstacles, and certified hardware/deployment profiles.

RBF-Safe remains safety infrastructure rather than another motion planner.
Paper experiments and RapidBoxForest legacy caches stay outside the standalone
library unless introduced as independently documented reproducibility assets.
