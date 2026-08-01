# `project.md` completion matrix

This matrix traces the original RBF-Safe Software Engineering Development
Plan to current public interfaces, behavioral tests, and maintained
documentation. The machine-readable source is
[`data/project_scope_manifest.json`](../data/project_scope_manifest.json);
`tools/check_project_scope.py` verifies every referenced file and symbol and
requires exactly one matrix entry for every requirement.

“Implemented” here means that repository evidence covers the software claim.
It does not turn software-only statements into physical safety evidence.
Hardware actuation, trusted perception, global time, and network consensus
remain external unless a dedicated interface explicitly says otherwise.

## Application layer

<!-- requirement: application.ik.safe -->
### Safe IK

- Plan claim: solve a target pose inside a safe region and return region plus
  connected-path evidence.
- Implementation: [`SafeIkSolver`](../include/rbfsafe/safe_ik.h), with
  region-constrained numerical solving and `CertifiedRoute` output.
- Verification: [`test_safe_ik.cpp`](../tests/test_safe_ik.cpp) and the
  [Safe IK guide](safe-ik.md).

<!-- requirement: application.learning.shield -->
### Learning and VLA action shield

- Plan claim: evaluate joint, end-effector, and trajectory actions as
  `ACCEPT`, `REPAIR`, or `REJECT`.
- Implementation: [`RuntimeShield`](../include/rbfsafe/shield.h) and
  `LearningPolicySafetyGate`.
- Verification: [`test_shield.cpp`](../tests/test_shield.cpp) and the
  [runtime shield guide](runtime-shield.md).

<!-- requirement: application.optimization.adapters -->
### Trajectory-optimization consumers

- Plan claim: make certified-region constraints consumable by TrajOpt, CHOMP,
  STOMP, and MPC workflows.
- Implementation: solver-neutral constraints plus the four named adapters in
  [`optimization.h`](../include/rbfsafe/optimization.h).
- Verification: [`test_optimization.cpp`](../tests/test_optimization.cpp) and
  the [optimization guide](optimization.md).

<!-- requirement: application.planning.moveit -->
### MoveIt integration

- Plan claim: provide safe sampling and planning-pipeline integration.
- Implementation: fail-closed request/response adapters, a constraint sampler,
  and a Safe IK `KinematicsBase` plugin in
  [`plugins/moveit2`](../plugins/moveit2/rbfsafe_moveit).
- Verification:
  [`test_plugin_loader.cpp`](../plugins/moveit2/rbfsafe_moveit/test/test_plugin_loader.cpp)
  and the [MoveIt 2 guide](moveit2.md).

<!-- requirement: application.planning.ompl -->
### OMPL integration

- Plan claim: provide certified validity and RRT, RRT*, PRM, and BIT*
  integration.
- Implementation: optional [`RBFSafe::ompl`](../include/rbfsafe/ompl.h)
  validity, motion, sampler, roadmap, and planner interfaces.
- Verification: [`test_ompl.cpp`](../tests/test_ompl.cpp) and the
  [OMPL adapter guide](ompl-adapter.md).

<!-- requirement: application.planning.output-audit -->
### Planner-output auditing

- Plan claim: classify arbitrary path outputs as `CERTIFIED`, `PARTIAL`, or
  `INVALID` with coverage.
- Implementation:
  [`TrajectoryAuditor`](../include/rbfsafe/trajectory.h), deterministic region
  sequence, and explicit uncovered intervals.
- Verification: [`test_trajectory.cpp`](../tests/test_trajectory.cpp) and the
  [trajectory auditor guide](trajectory-auditor.md).

## Core engine and data model

<!-- requirement: core.atlas.database-connectivity -->
### Safe Atlas and connectivity

- Plan claim: store regions/certificates and query membership, nearest region,
  connectivity, and routes.
- Implementation: [`SafeAtlas`](../include/rbfsafe/atlas.h), deterministic
  graph components, routes, persistence, and compatibility checks.
- Verification: [`test_atlas.cpp`](../tests/test_atlas.cpp) and the
  [API guide](api.md).

<!-- requirement: core.certification.region-corridor -->
### Region and corridor certification

- Plan claim: certify free regions and connected motion corridors.
- Implementation: `IfkAaLinkAabbValidator`, `ObbRegionValidator`, shared
  witness portals, and `HipacCorridorBuilder`.
- Verification: [`test_hipac.cpp`](../tests/test_hipac.cpp) and the
  [corridor guide](corridors.md).

<!-- requirement: core.geometry.fk-jacobian-envelope -->
### Geometry kernel

- Plan claim: provide FK, Jacobian, and conservative link envelopes.
- Implementation: point FK, pose FK, analytic 6-by-N geometric Jacobian, and
  IFK-AA/LinkIAABB envelopes in
  [`model.h`](../include/rbfsafe/model.h) and
  [`geometry.h`](../include/rbfsafe/geometry.h).
- Verification: analytic/finite-difference and 10,000-sample containment
  regressions in [`test_geometry.cpp`](../tests/test_geometry.cpp), documented
  in [kinematics](kinematics.md).

<!-- requirement: core.partition.lect-hipac -->
### LECT and HiPaC

- Plan claim: deterministic adaptive partitioning and recursive corridor
  covering.
- Implementation: stable-path [`LectTree`](../include/rbfsafe/lect.h) and
  [`HipacCorridorBuilder`](../include/rbfsafe/corridor.h).
- Verification: [`test_lect.cpp`](../tests/test_lect.cpp),
  [`test_hipac.cpp`](../tests/test_hipac.cpp), and the
  [corridor guide](corridors.md).

<!-- requirement: core.update.dynamic-repair -->
### Dynamic scene update and safety memory

- Plan claim: invalidate stale certificates, locally rebuild, and maintain
  versioned safety knowledge.
- Implementation: [`AtlasUpdater`](../include/rbfsafe/dynamic.h), dependency
  envelopes, local repair domains, and immutable version stores.
- Verification: [`test_dynamic.cpp`](../tests/test_dynamic.cpp) and the
  [dynamic update guide](dynamic-updates.md).

<!-- requirement: data.certificate-levels -->
### Evidence levels

- Plan claim: distinguish unknown, point checks, certified regions, certified
  connectivity, and runtime-executable decisions.
- Implementation:
  [`EvidenceLevel`](../include/rbfsafe/certificate.h) and subject-bound
  deterministic certificates.
- Verification: execution tests and the [safety model](safety-model.md);
  sampling never upgrades region evidence.

<!-- requirement: data.safe-region-family -->
### Generalized safe-region family

- Plan claim: represent AABB, OBB, Portal, TrajectoryTube, and later
  zonotope/Taylor regions.
- Implementation:
  [`RegionDatabase`](../include/rbfsafe/region_database.h) with all six
  geometry families and subject-bound certificates.
- Verification:
  [`test_region_database.cpp`](../tests/test_region_database.cpp) and the
  [region database guide](region-database.md).

<!-- requirement: interface.cpp-python-query -->
### C++ and Python safety APIs

- Plan claim: build, persist, query, visualize, audit, solve Safe IK, update,
  and shield through reusable APIs.
- Implementation: installable CMake targets under `RBFSafe::*`, aggregate
  [`rbfsafe.h`](../include/rbfsafe/rbfsafe.h), and the high-level
  [`rbfsafe`](../python/rbfsafe/__init__.py) package.
- Verification: independent downstream CMake consumption, wheel installation,
  [`test_python.py`](../tests/test_python.py), and
  [getting started](getting-started.md).

## Product releases and quality

<!-- requirement: quality.benchmarks -->
### Safety, efficiency, and coverage benchmarks

- Plan claim: cover IIWA, UR5, Panda, Franka and shelf, clutter,
  industrial-cell, and mobile-manipulation scenarios.
- Implementation: deterministic release fixtures and
  [`release_benchmark.cpp`](../benchmarks/release_benchmark.cpp), including
  false-safe, memory, update-time, and certified-path metrics.
- Verification: the [release fixture contract](release-fixtures.md), logical
  digest gate, fixed-format replay, and bounded soak runs.

<!-- requirement: release.v1-geometric-library -->
### Version 1 product scope

- Plan claim: safe regions, planner integration, Safe IK, and audit.
- Evidence: v0.1–v1.0 in the [roadmap](roadmap.md), the corresponding public
  components, golden robot/scene regressions, and cross-platform consumers.

<!-- requirement: release.v2-learning-runtime -->
### Version 2 product scope

- Plan claim: VLA/learning-policy safety, runtime shield, and runtime monitor.
- Evidence: `LearningPolicySafetyGate`, `RuntimeShield`,
  `RuntimeShieldMonitor`, policy/shield tests, feedback persistence, and v2.0
  in the [roadmap](roadmap.md).

<!-- requirement: release.v3-memory-multirobot-deployment -->
### Version 3 product scope

- Plan claim: persistent cross-task safety memory, multi-robot safety, and
  industrial deployment evidence.
- Evidence: `SafetyMemoryStore`, fleet schedules and continuous occupancy,
  coordinated reservations, reviewed deployment profiles, bounded execution
  sessions, transparency/provenance records, and v3.0–v4.7 in the
  [roadmap](roadmap.md).

## Remaining non-plan extensions

The original software plan is covered by the requirements above. Potential
future versions can add authenticated remote reservation-head gossip,
vendor-backed perception/prediction adapters, or controller-specific
admission. Those are new explicit trust and distributed-systems scopes rather
than missing implementations of the original geometric safety library plan.
