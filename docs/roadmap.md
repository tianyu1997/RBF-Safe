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
- **v0.7 (current):** unified AABB/OBB/Portal/TrajectoryTube/zonotope/Taylor
  certificate records, arbitrary AABB/OBB Portal discovery, deterministic OBB
  Atlas growth, correlation-preserving first-order Taylor IFK, schema-1
  region-database persistence, and C++/Python/CLI queries.

## Remaining `project.md` product phases

- **v0.8 — Planning and optimization consumers:** Atlas-guided OMPL
  RRT/RRT*/PRM/BIT* helpers, MoveIt certified sampling/roadmap acceleration,
  and solver-neutral convex-region constraints with TrajOpt, CHOMP, STOMP,
  and MPC adapters.
- **v0.9 — Learning and runtime shield:** joint, end-effector, and trajectory
  action checks with explicit `ACCEPT`, `REPAIR`, and `REJECT`; VLA-facing
  batching; bounded repair; telemetry; and a runtime monitor. Evidence remains
  below `RuntimeExecutable` until timing, tracking error, and deployment
  assumptions are explicitly modeled and verified.
- **v1.0 — Stable geometric safety library:** reviewed public API, documented
  schema migrations, long-term compatibility policy, benchmark/soak gates,
  and reproducible IIWA, UR5, Panda/Franka, shelf, clutter, industrial-cell,
  and mobile-manipulation fixtures.
- **v2.x — Intelligent safety layer:** uncertainty-aware runtime policies,
  persistent cross-task safety memory, policy-learning integrations, and
  execution evidence for explicitly supported deployment profiles.
- **v3.x — Safety memory infrastructure:** cross-task reuse, multi-robot
  coordination, fleet versioning, and industrial lifecycle tooling.

RBF-Safe remains safety infrastructure rather than another motion planner.
Paper experiments and RapidBoxForest legacy caches stay outside the standalone
library unless introduced as independently documented reproducibility assets.
