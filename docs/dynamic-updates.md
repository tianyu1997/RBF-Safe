# Dynamic scene updates and Atlas versions

RBF-Safe v0.6 updates a certified Atlas without treating a changed scene
digest as sufficient evidence. Every retained region follows one of two
paths: a direct validation against the new snapshot, or a checked transition
from its exact prior certificate.

## Scene differences

`compare_scenes(before, after)` compares stable obstacle IDs and returns a
deterministically ordered `SceneDelta`. Geometry-equal snapshots with a new
version produce a version-only delta. Added, removed, and modified obstacles
retain exact before/after workspace AABBs and the old/new scene identities.

## Certificate inheritance rule

A prior regional certificate may be inherited only when all conditions hold:

1. robot and old-scene identities exactly match the prior Atlas;
2. the parent certificate identity is valid and its subject is the same
   C-space AABB;
3. validator name, validator version, and obstacle padding are unchanged;
4. a complete stored link envelope is available; and
5. every added or modified obstacle's new AABB is disjoint from every link
   envelope AABB.

Removed obstacles cannot invalidate freedom. The new clearance lower bound is
the minimum of the old bound and distances to all added/modified obstacles.
The derived certificate binds the new scene, parent certificate, exact region,
and complete `SceneDelta` digest. Any failed precondition triggers direct
validation; an undetermined result invalidates the region.

## Local repair

Invalidated regions become bounded repair domains. A local deterministic LECT
starts at each domain, validates both children of every split, and continues
refinement only through unresolved children containing automatic or supplied
repair samples. Validation, node, depth, width, and cancellation limits are
hard bounds.

Certified children are added with new subject-bound certificates. Unresolved
domains are persisted. When an obstacle is removed or modified, those domains
are retried; a newly certified larger domain supersedes contained smaller
regions. This permits coverage to contract and later recover without a global
Atlas rebuild.

Sampling controls exploration only. It never upgrades evidence and is not a
collision-freedom proof.

## C++ example

```cpp
#include <rbfsafe/dynamic.h>

auto update = rbfsafe::AtlasUpdater{}.update(
    robot, previous_scene, next_scene, previous_atlas,
    {{0.25}}, rbfsafe::AtlasUpdateOptions{});
if (!update)
    return 1;

update.value().atlas.save("atlas-v2");
```

The result reports retained, invalidated, and repaired region IDs plus exact
validation and repair statistics.

## Python example

```python
update = rbfsafe.AtlasUpdater().update(
    robot, previous_scene, next_scene, previous_atlas,
    repair_samples=[[0.25]],
)
update.atlas.save("atlas-v2")
```

The CLI performs the same operation:

```bash
rbfsafe-inspect atlas-v1 \
  --robot robot.json \
  --previous-scene scene-v1.json \
  --next-scene scene-v2.json \
  --update-output atlas-v2
```

## Version store

`AtlasVersionStore` publishes immutable Atlas directories under
`versions/<version-id>/` and atomically updates `store.json`. Publishing
requires the candidate's parent to equal the active head and its sequence to
increase by one. The store checks the full scene transition and every inherited
certificate against the parent Atlas before publication or load.
Store creation requires a schema-2 initial Atlas with sequence zero. Rebuild a
schema-1 Atlas with `AtlasBuilder` before using it as a version-store root; a
schema-1 Atlas may still be updated as standalone historical input.

Rollback changes only the active head; it never deletes descendants. A later
publish may therefore create a branch from the selected ancestor.

```cpp
auto store = rbfsafe::AtlasVersionStore::create("atlas-store", initial_atlas);
store.value().publish(update.value().atlas);
store.value().rollback(initial_atlas.version_info().id);
```

Use `rbfsafe-inspect atlas-store` to inspect the active version, or
`--store-version <id>` to inspect an immutable historical version.

## Safety boundary

Dynamic updates cover static AABB snapshots with stable obstacle identities.
They do not infer swept volumes, sensor uncertainty, self-collision changes,
or execution-time guarantees. A consumer must first convert those effects to
conservative AABBs and publish a new snapshot version. No v0.6 component emits
`RuntimeExecutable` evidence.
