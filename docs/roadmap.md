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
- **v3.0 (current) - Robot safety memory:** deterministic persistent artifact
  catalog, monotonic lifecycle and replayable audit log, exact-identity
  cross-task reuse, scene-wide invalidation, multi-robot fleet snapshots, and
  conservative time-window workspace-reservation analysis. The independent
  schema-1 memory format, C++/Python/CLI interfaces, resource and cancellation
  gates, and industrial deployment guidance are included.

## Continued product hardening

- **later v3.x:** authenticated artifact services, calibrated policy metadata,
  transactional multi-process memory backends, fleet schedule persistence and
  versioning, and execution evidence only for separately modeled and reviewed
  deployment profiles.
- **v4.x candidates:** richer continuous-time multi-robot occupancy proofs,
  distributed fleet coordination, and certified hardware/deployment profiles.

RBF-Safe remains safety infrastructure rather than another motion planner.
Paper experiments and RapidBoxForest legacy caches stay outside the standalone
library unless introduced as independently documented reproducibility assets.
