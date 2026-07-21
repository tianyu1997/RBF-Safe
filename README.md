# RBF-Safe

[![CI](https://github.com/tianyu1997/RBF-Safe/actions/workflows/ci.yml/badge.svg)](https://github.com/tianyu1997/RBF-Safe/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](CMakeLists.txt)
[![Python](https://img.shields.io/badge/Python-3.10--3.12-blue.svg)](pyproject.toml)

RBF-Safe is a C++20 and Python library for building reusable, conservative
geometric safety certificates in robot configuration space. Version 0.2
supports serial DH robots, workspace AABB obstacles, a public deterministic
LECT partition, certified C-space AABB regions, connectivity queries, and a
portable versioned atlas format. It also audits continuous piecewise-linear
trajectories against the certified region union.

RBF-Safe is safety infrastructure, not a motion planner. A region is marked
`CertifiedRegion` only when conservative affine-arithmetic forward-kinematics
envelopes prove every represented link volume disjoint from every obstacle.
Sampling guides refinement and tests the implementation; it never creates a
certificate.

## Capabilities

- Deterministic IFK-AA + LinkIAABB regional certification.
- Public mutable `LectTree` and immutable `LectSnapshot` APIs.
- Seed-guided `SafeAtlas` construction, region lookup, nearest-region lookup,
  and certificate-connectivity queries.
- Robot/scene identity binding with SHA-256.
- Checksummed, explicitly little-endian atlas schema v1.
- CMake install/export targets and a high-level `rbfsafe` Python package.
- `rbfsafe-inspect` metadata, validation, query, and optional 2-D slice tools.
- Continuous piecewise-linear trajectory auditing with explicit uncovered
  parameter intervals and deterministic region sequences.

Motion planning, OMPL/MoveIt adapters, Safe IK, dynamic scene repair, OBBs,
portals, and legacy cache compatibility are intentionally outside v0.2.

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

Inspect the result with either installed CLI:

```bash
rbfsafe-inspect atlas --query 0.0 0.0  # Python entry point
rbfsafe-inspect atlas 0.0 0.0          # C++ executable
rbfsafe-inspect atlas --trajectory data/trajectory_2r.json  # Python entry point
```

## Documentation

- [Installation](docs/installation.md)
- [Getting started](docs/getting-started.md)
- [Input formats](docs/input-formats.md)
- [Architecture](docs/architecture.md)
- [API overview](docs/api.md)
- [Safety model](docs/safety-model.md)
- [Trajectory auditor](docs/trajectory-auditor.md)
- [Atlas schema v1](docs/atlas-format.md)
- [Versioning and compatibility](docs/versioning.md)
- [Migration map](docs/migration-map.md) and [provenance](docs/provenance.md)
- [Roadmap](docs/roadmap.md)

Read the safety model before using RBF-Safe in a robot system. A certificate
is a geometric software claim under its recorded model and scene; it is not a
deployed-system safety certification or an execution guarantee.

## Contributing and support

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and testing rules,
[SUPPORT.md](SUPPORT.md) for support channels, and [SECURITY.md](SECURITY.md)
for private vulnerability and incorrect-certificate reports.

RBF-Safe is available under the [MIT License](LICENSE). The C++ API is pre-1.0
and does not yet promise ABI stability; the atlas schema is versioned
independently. See [CHANGELOG.md](CHANGELOG.md) for release notes.
