# Roadmap

- **v0.1:** DH+AABB geometry, IFK-AA/LinkIAABB certificates, public LECT,
  SafeAtlas build/query/persistence, C++ and Python packages.
- **v0.2:** continuous trajectory auditor with `CERTIFIED`,
  `PARTIAL`, and `INVALID` reports, coverage ratio, deterministic region
  sequence, and explicit uncovered intervals.
- **v0.3:** optional OMPL component with certified-only state
  validity, continuous motion validation, and certified-region sampling.
- **v0.4:** conservative OBB segment tubes, bounded growth,
  shared-witness portals, HiPaC recursive covering, and subject-bound
  connectivity routes.
- **v0.5 (current):** deterministic region-constrained Safe IK, explicit
  Atlas routes and connectivity certificates, and fail-closed ROS 2 Jazzy /
  MoveIt 2 adapters and kinematics plugin.
- **v0.6:** scene invalidation, local repair, and incremental atlas versions.
- **v1.0:** stable public API and documented storage migrations.

Dynamic update, VLA shields, planner implementations, correlated OBB proofs,
arbitrary OBB portal discovery, execution certification, and paper experiments
are intentionally outside v0.5.
