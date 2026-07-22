# Schema support and migrations

Library SemVer and storage schema numbers are independent. RBF-Safe 2.0 reads
every standalone format released by 0.x and never interprets a legacy
RapidBoxForest cache as RBF-Safe data.

| Format | Read | Write | Migration in 2.0 |
|---|---:|---:|---|
| Robot JSON | 1 | 1 | None required |
| Scene JSON | 1 | 1 | None required |
| Standalone LECT | 1 | 1 | None required |
| Atlas | 1, 2 | 2 for new/update; preserve 1 on plain save | Schema 1 to 2 by full regional revalidation through `AtlasUpdater` |
| HiPaC corridor | 1 | 1 | None required |
| Region database | 1 | 1 | None required |
| Atlas version store | 1 | 1 | Contained Atlas versions retain their own schema |
| Policy feedback | 1 | 1 | New independent format; no legacy format is interpreted |

Unknown schemas fail with `IncompatibleFormat`; malformed known schemas fail
with `CorruptData` or `ResourceLimit`. There is no implicit downgrade.

## Atlas schema 1 to 2

Schema 1 lacks exact regional subject digests, link-envelope dependencies,
repair domains, transition documents, and version lineage. Those fields
cannot be invented from old bytes. Migration therefore requires the exact
robot and old scene plus a distinct target `SceneSnapshot`, and performs full
regional validation:

```python
legacy = rbfsafe.SafeAtlas.load("atlas-v1")
robot = rbfsafe.SerialRobotModel.from_json("robot.json")
old_scene = rbfsafe.SceneSnapshot.from_json("scene-v1.json")
new_scene = rbfsafe.SceneSnapshot(
    old_scene.obstacles,
    "scene-v1-schema2-migration",
)
migrated = rbfsafe.AtlasUpdater().update(robot, old_scene, new_scene, legacy)
assert migrated.atlas.storage_schema == 2
migrated.atlas.save("atlas-v2")
```

Changing only the scene version makes the identity transition explicit while
keeping obstacle geometry identical. Because schema-1 certificates cannot be
inherited, `certificates_inherited` is zero and every retained region appears
in `regions_revalidated`. The fixed `data/atlas_schema1` artifact is loaded,
byte-preserved, migrated this way, and validated on Linux and Windows CI.

## 2.x format policy

- Every new schema receives a separate specification, bounded reader, fixed
  cross-platform fixture, corruption tests, and explicit migration or
  incompatibility behavior before release.
- Readers for schemas supported by 2.0 remain available throughout 2.x.
- Writers publish atomically and never overwrite by default.
- Migration is always explicit and writes a new destination; input artifacts
  remain untouched.
- Certificate evidence is revalidated whenever an older schema lacks fields
  needed to justify inheritance.

Schema removal or reinterpretation requires a major library release. A future
writer may introduce a new schema in 2.x only while preserving these reader
and migration guarantees.
