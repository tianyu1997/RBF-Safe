# Changelog

All notable changes are documented here. The project follows Semantic
Versioning for library releases and versions its on-disk schemas separately.

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
