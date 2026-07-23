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
- `route(q1, q2)` returns a deterministic certified path through intersecting
  convex Atlas AABBs; `connected(q1, q2)` is its Boolean shorthand. HiPaC
  remains useful for covering a supplied candidate path with oriented cells.
  Neither API grants a runtime-execution guarantee.
- `false` normally means “not certified by this Atlas,” not “in collision.”

## Build a certified OBB corridor

When a planner or optimizer already produced a candidate polyline, v0.4 can
cover it with certified OBB cells:

```cpp
std::vector<rbfsafe::Configuration> path{
    {-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
auto report = rbfsafe::HipacCorridorBuilder{}.build(robot, scene, path);
if (report && report.value().status == rbfsafe::HipacBuildStatus::Certified) {
    auto route = report.value().corridor.route(path.front(), path.back());
    report.value().corridor.save("corridor");
}
```

The builder recursively splits unresolved segments and returns explicit gaps
for partial coverage. A returned route is a geometric connectivity
certificate through convex cells, not a timing or execution approval. See the
[corridor guide](corridors.md).

## 6. Solve a Safe IK query

Safe IK keeps its search inside certified regions and requires an explicit
Atlas route from the seed to the result:

```python
target = robot.end_effector_pose([0.4, -0.2])
report = rbfsafe.SafeIkSolver().solve(
    robot, scene, atlas, target, [0.0, 0.0]
)
if report.status == rbfsafe.SafeIkStatus.SAFE_CONNECTED:
    print(report.solution)
    print(report.connectivity_route.certificate.id)
```

Pose convergence is point-checked evidence. Read the [Safe IK guide](safe-ik.md)
before using the result in a planning or control system.

## 7. Audit a trajectory

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

## 8. Update a changed scene

Given the exact previous snapshot and a new snapshot:

```python
update = rbfsafe.AtlasUpdater().update(
    robot,
    previous_scene,
    next_scene,
    atlas,
    repair_samples=[[0.0, 0.0]],
)
update.atlas.save("atlas-v2")
print(update.stats.certificates_inherited)
print(update.invalidated_region_ids)
```

For persistent history, create `AtlasVersionStore` from the initial Atlas and
publish each derived version in parent order. Read
[dynamic updates](dynamic-updates.md) before using certificate inheritance or
rollback.

## 9. Register reusable safety memory

After saving an immutable artifact, catalog its exact identities and content
digest:

```python
item = rbfsafe.MemoryArtifactInput()
item.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
item.deployment_id = "arm-a"
item.robot_digest = robot.digest
item.scene_digest = scene.digest
item.task_id = "shelf-pick"
item.content_digest = atlas.version_info.id
item.locator = "artifacts/shelf-atlas"
item.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION

memory = rbfsafe.SafetyMemory()
artifact = memory.register_artifact(item)
memory.save("safety-memory")
```

Cross-task direct reuse still requires exact deployment, robot, and scene
identity. Scene changes must invalidate old records and publish a newly
validated artifact. Read [persistent safety memory](safety-memory.md) before
using the catalog or fleet coordination APIs.

For multiple processes, create a revision store and always publish against the
head you observed:

```python
store = rbfsafe.SafetyMemoryStore.create("safety-memory-store", memory)
expected = store.current_revision_id
memory.invalidate_scene("arm-a", scene.digest, "cell layout changed")
revision = store.publish(memory, expected)
```

A stale `expected` value raises `IdentityMismatchError`; no newer revision is
overwritten. See [transactional safety memory](safety-memory-store.md).

## 10. Persist fleet-schedule history

Publish canonical reservation reports against the exact memory revision used
to validate their source artifacts:

```python
archive = rbfsafe.FleetScheduleArchive.create(fleet.fleet_id)
root = archive.publish(fleet, memory, reservations, "")
current = archive.publish(fleet, memory, revised_reservations, root.id)
archive.save("fleet-schedules")

loaded = rbfsafe.FleetScheduleArchive.load("fleet-schedules")
loaded.verify_version(current.id, fleet, memory)
```

Preserve that memory revision if the live catalog is later changed. The
archive status remains a declared-envelope coordination result, not an
execution certificate. See [versioned fleet schedules](fleet-schedule-archive.md).
