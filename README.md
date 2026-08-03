# RBF-Safe

[简体中文](README.zh-CN.md) | English

[![CI](https://github.com/tianyu1997/RBF-Safe/actions/workflows/ci.yml/badge.svg)](https://github.com/tianyu1997/RBF-Safe/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](CMakeLists.txt)
[![Python](https://img.shields.io/badge/Python-3.10--3.12-blue.svg)](pyproject.toml)

RBF-Safe 4.7 is a C++20 and Python library for building, storing, querying, and reusing conservative geometric safety certificates in robot configuration space. It supports modified-DH serial robots, AABB scenes, deterministic LECT partitioning, certified AABB/OBB regions and corridors, connectivity, trajectory auditing, planning integrations, dynamic updates, policy shields, persistent safety memory, and auditable multi-robot coordination.

RBF-Safe is safety infrastructure, not a motion planner or a deployed-system safety certification. Sampling, visualization, successful planning, valid signatures, and consistent logs never upgrade evidence by themselves. Read the [safety model](docs/safety-model.md) before using a result in a robot system.

## Main components

| Component | Purpose |
|---|---|
| `RBFSafe::geometry` | Modified-DH FK, analytic geometric Jacobian, IFK-AA, and conservative link envelopes |
| `RBFSafe::lect` | Deterministic mutable trees and immutable snapshots with stable path keys |
| `RBFSafe::atlas` | Certified-region construction, lookup, connectivity, routing, and versioned persistence |
| `RBFSafe::corridor`, `RBFSafe::regions` | OBB/Portal/HiPaC corridors and a unified certified-region database |
| `RBFSafe::planning`, `RBFSafe::ik`, optional `RBFSafe::ompl` | Certified sampling, Safe IK, audited planning, OMPL, MoveIt 2, and optimization consumers |
| `RBFSafe::update` | Scene differences, conservative inheritance/invalidation, local repair, and immutable Atlas versions |
| `RBFSafe::shield`, `RBFSafe::policy` | Deterministic action gating, bounded repair, policy metadata checks, calibration, and feedback |
| `RBFSafe::memory` | Identity-bound artifacts, lifecycle history, reuse, fleet snapshots, and reservation analysis |
| `RBFSafe::trust`, `RBFSafe::remote`, `RBFSafe::identity` | Artifact attestations, transport-neutral exchange, Ed25519 identities, and trust rotation |
| `RBFSafe::deployment`, `RBFSafe::execution` | Reviewed deployment profiles, bounded sessions, and revocation-aware authorization ledgers |
| `RBFSafe::transparency`, `RBFSafe::witness`, `RBFSafe::provenance` | Merkle logs, checkpoint gossip, split-view detection, hardware statements, and external time |
| `RBFSafe::occupancy`, `RBFSafe::coordination` | Continuous swept occupancy, authenticated publication histories, and coordinated reservations |

The [documentation index](docs/README.md) links each component to its usage guide, exact storage schema, compatibility rules, and trust boundary.

## Build and test

```bash
git clone https://github.com/tianyu1997/RBF-Safe.git
cd RBF-Safe
cmake -S . -B build \
  -DRBFSAFE_BUILD_TESTS=ON \
  -DRBFSAFE_BUILD_EXAMPLES=ON \
  -DRBFSAFE_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Install for a downstream CMake project:

```bash
cmake --install build --config Release --prefix install
```

```cmake
find_package(RBFSafe 4.7 REQUIRED)
target_link_libraries(my_target PRIVATE RBFSafe::rbfsafe)
```

The OMPL adapter is optional (`-DRBFSAFE_BUILD_OMPL=ON`). MoveIt 2 integration is built separately from [`plugins/moveit2`](plugins/moveit2/rbfsafe_moveit/README.md), so ROS dependencies never enter the core library.

## C++ quick start

```cpp
#include <rbfsafe/rbfsafe.h>

using namespace rbfsafe;

int main() {
    auto robot = SerialRobotModel::create(
        "planar-2r",
        {{0.0, 1.0, 0.0, 0.0, JointType::Revolute},
         {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}},
        {0.05, 0.05});
    if (!robot)
        return 1;

    SceneSnapshot scene({}, "empty-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
    if (!built)
        return 1;

    built.value().atlas.save("atlas");
    return built.value().atlas.contains(Configuration{0.0, 0.0}) ? 0 : 1;
}
```

## Python quick start

Build and install a wheel from a clean checkout:

```bash
python -m pip install build
python -m build --wheel
python -m pip install dist/rbfsafe-*.whl
```

```python
import rbfsafe

robot = rbfsafe.SerialRobotModel.from_json("data/planar_2r.json")
scene = rbfsafe.SceneSnapshot.from_json("data/empty_scene.json")
built = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]])
built.atlas.save("atlas")
print(built.atlas.contains([0.0, 0.0]))
```

Inspect saved artifacts without writing code:

```bash
rbfsafe-inspect atlas --query 0.0 0.0
rbfsafe-inspect atlas --trajectory data/trajectory_2r.json
rbfsafe-inspect atlas --plot slice.png --dims 0 1
```

See [Getting started](docs/getting-started.md) for corridors, Safe IK, planning, shields, policy gates, safety memory, occupancy, trust, deployment, and execution examples.

## Repository layout

```text
include/rbfsafe/   Public C++ API
src/               C++ implementations grouped by component
python/            pybind11 bindings and the rbfsafe package
tests/             C++ and installed-wheel regression tests
data/              Small inputs and fixed cross-version fixtures
examples/          Focused C++ and Python quick starts
tools/             CLI and repository consistency checks
docs/              Guides, formats, compatibility, and release procedures
plugins/moveit2/   Optional ROS 2 Jazzy integration
benchmarks/        Deterministic release benchmark and thresholds
third_party/       Vendored code with retained license notices
```

Build trees, wheels, caches, test output, and local install prefixes are intentionally ignored and must not be committed.

## Documentation

- [Complete English documentation index](docs/README.md)
- [Installation](docs/installation.md)
- [Getting started](docs/getting-started.md)
- [API overview](docs/api.md)
- [Architecture](docs/architecture.md)
- [Safety model](docs/safety-model.md)
- [Versioning and compatibility](docs/versioning.md)
- [Curated Simplified Chinese documentation](docs/zh-CN/README.md)
- [Project-scope traceability matrix](docs/project-scope-matrix.md)

## Compatibility and support

The 4.x public C++ and Python APIs follow the documented source-compatibility policy; a universal C++ binary ABI is not promised. Storage schemas are versioned independently and validated before use. RapidBoxForest caches are not RBF-Safe files.

See [CONTRIBUTING.md](CONTRIBUTING.md) for development rules, [SUPPORT.md](SUPPORT.md) for support channels, [SECURITY.md](SECURITY.md) for private vulnerability or incorrect-certificate reports, and [CHANGELOG.md](CHANGELOG.md) for release notes.

RBF-Safe is available under the [MIT License](LICENSE). Third-party notices are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
