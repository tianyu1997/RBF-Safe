# Getting started

This guide builds, saves, loads, and queries a small Atlas. Read the
[safety model](safety-model.md) before interpreting a successful query.

## 1. Define the inputs

RBF-Safe accepts a serial modified-DH robot and an immutable AABB scene. The
repository includes `data/planar_2r.json` and `data/empty_scene.json` as small
examples. See [input formats](input-formats.md) for field-level definitions.

Seeds are configurations of length equal to the robot dimension. They direct
which unresolved LECT branches are refined; they are not evidence.

## 2. Build and query in C++

```cpp
#include <rbfsafe/rbfsafe.h>

#include <iostream>

int main() {
    auto robot = rbfsafe::SerialRobotModel::from_json("data/planar_2r.json");
    auto scene = rbfsafe::SceneSnapshot::from_json("data/empty_scene.json");
    if (!robot || !scene) return 1;

    rbfsafe::BuildOptions options;
    options.maximum_depth = 24;
    options.maximum_nodes = 1'000'000;
    options.minimum_normalized_width = 1e-3;
    options.threads = 1;

    auto result = rbfsafe::AtlasBuilder{}.build(
        robot.value(), scene.value(), {{0.0, 0.0}, {1.0, -1.0}}, options);
    if (!result) {
        std::cerr << result.error().describe() << '\n';
        return 1;
    }

    auto& atlas = result.value().atlas;
    if (!atlas.save("atlas")) return 1;
    std::cout << atlas.contains(rbfsafe::Configuration{0.0, 0.0}) << '\n';

    auto loaded = rbfsafe::SafeAtlas::load("atlas");
    if (!loaded || !loaded.value().verify_compatible(robot.value(), scene.value())) return 1;
    return 0;
}
```

Always call `verify_compatible` when the current robot and scene did not
directly produce the in-memory Atlas.

## 3. Build and query in Python

```python
from pathlib import Path

import rbfsafe

robot = rbfsafe.SerialRobotModel.from_json("data/planar_2r.json")
scene = rbfsafe.SceneSnapshot.from_json("data/empty_scene.json")

options = rbfsafe.BuildOptions()
options.maximum_depth = 24
options.maximum_nodes = 1_000_000
options.threads = 1

result = rbfsafe.AtlasBuilder().build(
    robot, scene, [[0.0, 0.0], [1.0, -1.0]], options
)
result.atlas.save(Path("atlas"))

atlas = rbfsafe.SafeAtlas.load(Path("atlas"))
atlas.verify_compatible(robot, scene)
print(atlas.contains([0.0, 0.0]))
print([region.id for region in atlas.regions_at([0.0, 0.0])])
print(atlas.connected([0.0, 0.0], [1.0, -1.0]))
```

Python maps invalid inputs to `ValueError`, I/O failures to `OSError`, resource
limits to `MemoryError`, and identity/format/corruption/cancellation/internal
failures to subclasses of `RBFSafeError`.

## 4. Inspect and visualize

```bash
rbfsafe-inspect atlas --query 0.0 0.0
rbfsafe-inspect atlas --plot slice.png --dims 0 1
```

The plot is a visualization of stored certified regions, not an independent
certificate verifier.

## 5. Interpret the result

- `contains(q)` means at least one stored `CertifiedRegion` contains `q`.
- `regions_at(q)` returns every matching region.
- `nearest_region(q)` is a geometric nearest-box query and does not certify
  the segment from `q` to that region.
- `connected(q1, q2)` means both points belong to the same certified region
  component. v0.2 does not return a generated path or a runtime-execution
  guarantee.
- `false` normally means “not certified by this Atlas,” not “in collision.”

## 6. Audit a trajectory

After loading and compatibility-checking an Atlas:

```python
report = rbfsafe.TrajectoryAuditor().audit(
    atlas,
    [[-1.0, 0.0], [0.0, 0.0], [1.0, 0.0]],
)
print(report.status, report.coverage_ratio)
```

Read the [trajectory auditor guide](trajectory-auditor.md) before interpreting
`PARTIAL` or `INVALID`.
