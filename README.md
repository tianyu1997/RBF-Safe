# RBF-Safe

[![CI](https://github.com/tianyu1997/RBF-Safe/actions/workflows/ci.yml/badge.svg)](https://github.com/tianyu1997/RBF-Safe/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](CMakeLists.txt)
[![Python](https://img.shields.io/badge/Python-3.10--3.12-blue.svg)](pyproject.toml)

RBF-Safe is a C++20 and Python library for building reusable, conservative
geometric safety certificates in robot configuration space. Version 3.1
supports serial DH robots, workspace AABB obstacles, a public deterministic
LECT partition, certified C-space AABB regions, connectivity queries, and a
portable versioned atlas format. It also audits continuous piecewise-linear
trajectories against the certified region union.

An optional C++ OMPL component maps certified Atlas coverage to state and
continuous-motion validity and samples directly from certified regions.
The core corridor component builds conservative OBB tubes, shared-witness
portals, and certified HiPaC routes around candidate paths.
The Safe IK component solves end-effector pose targets inside certified Atlas
regions and returns an explicit Atlas connectivity certificate. An optional
ROS 2 Jazzy package exposes fail-closed MoveIt 2 request, response, and
kinematics plugins without adding ROS dependencies to the core library.
The dynamic-update component compares versioned scenes, conservatively
inherits or invalidates regional evidence, locally repairs affected space, and
publishes auditable immutable Atlas versions with rollback.
The generalized region layer unifies AABB, OBB, Portal, TrajectoryTube,
zonotope, and first-order Taylor records, discovers arbitrary certified
AABB/OBB intersections, and builds deterministic seed-driven OBB Atlases.
The planning-consumer layer adds reusable certified sampling and exact-witness
roadmaps; the OMPL helper runs and audits RRT, RRT*, PRM, and BIT*. The
optimization layer compiles heterogeneous convex regions into solver-neutral
TrajOpt/CHOMP/STOMP/MPC constraints.
The runtime shield checks joint, end-effector, and trajectory proposals,
performs bounded certified repair, batches VLA proposals, records telemetry,
and monitors execution observations without overstating runtime evidence.
The learning-policy safety layer additionally gates proposals on declared
confidence, uncertainty, freshness, and inference latency, deterministically
selects a shield-accepted or repaired action, and persists aligned,
identity-bound training feedback.
The persistent safety-memory layer catalogs immutable safety artifacts with
monotonic lifecycle and replayable audit events, permits exact-identity
cross-task reuse, and checks multi-robot workspace reservations under bounded
declared-envelope assumptions. Immutable safety-memory revision stores add
expected-head transactions and fail-closed cross-process publication.

RBF-Safe is safety infrastructure, not a motion planner. A region is marked
`CertifiedRegion` only when conservative affine-arithmetic forward-kinematics
envelopes prove every represented link volume disjoint from every obstacle.
Sampling guides refinement and tests the implementation; it never creates a
certificate.

## Capabilities

- Deterministic IFK-AA + LinkIAABB regional certification.
- Public mutable `LectTree` and immutable `LectSnapshot` APIs.
- Seed-guided `SafeAtlas` construction, region lookup, nearest-region lookup,
  certificate-connectivity queries, and an immutable region query BVH.
- Robot/scene identity binding with SHA-256.
- Checksummed, explicitly little-endian Atlas schema 2 with schema-1 loading.
- CMake install/export targets and a high-level `rbfsafe` Python package.
- `rbfsafe-inspect` metadata, validation, query, and optional 2-D slice tools.
- Continuous piecewise-linear trajectory auditing with explicit uncovered
  parameter intervals and deterministic region sequences.
- Public `RBFSafe::planning` certified region sampler and exact-intersection
  roadmap seed with explicit budgets, identity checks, and cancellation.
- Optional `RBFSafe::ompl` adapter with certified-only state checking,
  continuous edge validation, guided/default sampling modes, and audited
  RRT/RRT*/PRM/BIT* helpers.
- Public `RBFSafe::corridor` OBB/Portal/HiPaC layer with bounded growth,
  partial-coverage reports, certified route recovery, and schema-1 storage.
- Public `Pose3d`, deterministic `RBFSafe::ik`, and subject-bound Atlas route
  certificates for region-constrained Safe IK.
- Optional ROS 2 Jazzy `rbfsafe_moveit` package with certified start-state,
  final-trajectory, connected Safe IK gates, and Atlas/roadmap-biased
  constraint sampling.
- Public `RBFSafe::update` scene differences, envelope-backed certificate
  inheritance, local repair/recovery, and immutable Atlas version stores.
- Public `RBFSafe::regions` unified certificate database, arbitrary convex
  AABB/OBB Portals, OBB Atlas builder, higher-order correlated IFK, and
  checksummed schema-1 persistence.
- Public `RBFSafe::optimization` direct/lifted convex constraints, residuals,
  gradients, bounded projection, waypoint assignment, and named adapters for
  TrajOpt, CHOMP, STOMP, and MPC.
- Public `RBFSafe::shield` action checks with deterministic
  `ACCEPT`/`REPAIR`/`REJECT` decisions, bounded repair, VLA proposal batching,
  synchronized telemetry, and an Atlas-backed runtime monitor.
- Public `RBFSafe::policy` uncertainty/freshness gates, deterministic
  learned-policy selection, aligned feedback labels, telemetry, bounded
  queries, and checksummed schema-1 feedback persistence.
- Public `RBFSafe::memory` persistent artifact catalog, lifecycle/audit log,
  exact-identity cross-task reuse, scene invalidation, fleet snapshots,
  reservation conflict analysis, checksummed schema-1 persistence, and an
  immutable optimistic-concurrency revision store.
- Reviewed 3.x public source API, documented storage migrations, deterministic
  release benchmark/soak gates, and reproducible named release fixtures.

RBF-Safe configures upstream OMPL planners but does not reimplement them.
Higher-order Portal discovery,
continuous-time obstacle motion, authenticated/calibrated policy metadata,
continuous-time fleet occupancy proofs, authenticated artifact services,
execution guarantees, and legacy RapidBoxForest cache compatibility remain
outside v3.1.

## Quick start

```bash
git clone https://github.com/tianyu1997/RBF-Safe.git
cd RBF-Safe
cmake -S . -B build -DRBFSAFE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

```cpp
#include <rbfsafe/rbfsafe.h>

using namespace rbfsafe;

auto robot = SerialRobotModel::create(
    "planar-2r",
    {{0.0, 1.0, 0.0, 0.0, JointType::Revolute},
     {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
    {{-1.5, 1.5}, {-1.5, 1.5}},
    {0.05, 0.05});
if (!robot) return 1;

SceneSnapshot scene({}, "empty-v1");
auto result = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
if (!result) return 1;

result.value().atlas.save("atlas");
bool certified = result.value().atlas.contains(Configuration{0.0, 0.0});
```

Build a Python wheel from a clean environment:

```bash
python -m pip install build
python -m build --wheel
python -m pip install dist/rbfsafe-*.whl
```

```python
import rbfsafe

robot = rbfsafe.SerialRobotModel.from_json("data/planar_2r.json")
scene = rbfsafe.SceneSnapshot.from_json("data/empty_scene.json")
result = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]])
result.atlas.save("atlas")
print(result.atlas.contains([0.0, 0.0]))
```

Gate a learned-policy joint action:

```python
shield = rbfsafe.RuntimeShield()
decision = shield.check_joint_action(
    robot,
    scene,
    result.atlas,
    [0.0, 0.0],
    rbfsafe.JointDeltaAction([0.1, -0.05]),
)
print(decision.outcome, decision.output_trajectory)
```

Gate and select a metadata-bearing learned-policy batch:

```python
metadata = rbfsafe.PolicyProposalMetadata()
metadata.policy_id = "vla-primary"
metadata.task_id = "shelf-pick"
metadata.confidence = 0.92
proposal = rbfsafe.PolicyProposal(
    rbfsafe.JointDeltaAction([0.1, -0.05]), metadata
)
options = rbfsafe.PolicyGateOptions()
options.minimum_confidence = 0.7
report = rbfsafe.LearningPolicySafetyGate().check_proposals(
    robot, scene, result.atlas, [0.0, 0.0], [proposal], options
)
rbfsafe.PolicyFeedbackDatabase.create(report.feedback).save("policy-feedback")
```

Register and reuse a persistent safety artifact:

```python
artifact = rbfsafe.MemoryArtifactInput()
artifact.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
artifact.deployment_id = "arm-a"
artifact.robot_digest = robot.digest
artifact.scene_digest = scene.digest
artifact.task_id = "shelf-pick"
artifact.content_digest = result.atlas.version_info.id
artifact.locator = "artifacts/shelf-atlas"
artifact.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION

memory = rbfsafe.SafetyMemory()
stored = memory.register_artifact(artifact)
query = rbfsafe.MemoryReuseQuery()
query.deployment_id = "arm-a"
query.robot_digest = robot.digest
query.scene_digest = scene.digest
query.target_task_id = "shelf-place"
print(memory.query_reuse(query)[0].disposition)
memory.save("safety-memory")
```

Incrementally update a schema-2 Atlas:

```python
updated = rbfsafe.AtlasUpdater().update(
    robot, previous_scene, next_scene, result.atlas
)
updated.atlas.save("atlas-v2")
```

Inspect the result with either installed CLI:

```bash
rbfsafe-inspect atlas --query 0.0 0.0  # Python entry point
rbfsafe-inspect atlas 0.0 0.0          # C++ executable
rbfsafe-inspect atlas --trajectory data/trajectory_2r.json  # Python entry point
rbfsafe-inspect corridor --query 0.0 0.0  # Atlas/corridor auto-detection
rbfsafe-inspect region-database --query 0.0 0.0 --include-portals
rbfsafe-inspect policy-feedback --policy-id vla-primary
rbfsafe-inspect safety-memory --deployment-id arm-a --include-memory-events
rbfsafe-inspect atlas --robot data/planar_2r.json --scene data/empty_scene.json \
  --ik-target 1.9 0.6 0 0 0 0.1 0.995 --seed 0 0
```

## Documentation

- [Installation](docs/installation.md)
- [Getting started](docs/getting-started.md)
- [Input formats](docs/input-formats.md)
- [Architecture](docs/architecture.md)
- [API overview](docs/api.md)
- [API stability policy](docs/api-stability.md)
- [Safety model](docs/safety-model.md)
- [Trajectory auditor](docs/trajectory-auditor.md)
- [OMPL adapter](docs/ompl-adapter.md)
- [Certified planning consumers](docs/planning-consumers.md)
- [Optimization adapters](docs/optimization.md)
- [Runtime action shield](docs/runtime-shield.md)
- [Learning-policy safety](docs/policy-safety.md)
- [Policy feedback schema v1](docs/policy-feedback-format.md)
- [Persistent safety memory and fleets](docs/safety-memory.md)
- [Safety memory schema v1](docs/safety-memory-format.md)
- [OBB corridors, portals, and HiPaC](docs/corridors.md)
- [Safe IK](docs/safe-ik.md)
- [MoveIt 2 integration](docs/moveit2.md)
- [Dynamic updates and version stores](docs/dynamic-updates.md)
- [Unified region database](docs/region-database.md)
- [Region database schema v1](docs/region-database-format.md)
- [Atlas schemas 1 and 2](docs/atlas-format.md)
- [Corridor schema v1](docs/corridor-format.md)
- [Versioning and compatibility](docs/versioning.md)
- [Schema support and migrations](docs/schema-migrations.md)
- [Release fixtures and benchmark](docs/release-fixtures.md)
- [Migration map](docs/migration-map.md) and [provenance](docs/provenance.md)
- [Roadmap](docs/roadmap.md)

Read the safety model before using RBF-Safe in a robot system. A certificate
is a geometric software claim under its recorded model and scene; it is not a
deployed-system safety certification or an execution guarantee.

## Contributing and support

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and testing rules,
[SUPPORT.md](SUPPORT.md) for support channels, and [SECURITY.md](SECURITY.md)
for private vulnerability and incorrect-certificate reports.

RBF-Safe is available under the [MIT License](LICENSE). The 3.x public C++ and
Python surfaces follow the documented source-compatibility policy; a universal
C++ binary ABI is not promised. Storage schemas are versioned independently.
See [CHANGELOG.md](CHANGELOG.md) for release notes.
