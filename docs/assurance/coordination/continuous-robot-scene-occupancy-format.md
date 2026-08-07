# Continuous robot-scene occupancy bundle schema 1

`ContinuousRobotSceneOccupancyBundle` uses one checksummed JSON file,
independent from Atlas, scene, fleet-occupancy, publication-history, and
execution schemas.

## Document

The top-level object has exactly these fields:

```json
{
  "checksum": "<sha256 of compact canonical payload JSON>",
  "format": "rbfsafe-continuous-robot-scene-occupancy-bundle",
  "library_version": "4.4.0",
  "payload": {},
  "schema": 1
}
```

`library_version` is informational. `schema` controls decoding. Unknown
formats or schemas return `IncompatibleFormat`; malformed, truncated, or
checksum-invalid data returns `CorruptData` or the applicable bounded I/O
error.

The payload has exactly:

- `storage_schema`, as an unsigned decimal string;
- deterministic `id`;
- canonically ordered complete `robot_occupancies`;
- canonically ordered complete `obstacle_occupancies`; and
- the complete deterministic `report`.

All `uint64_t` and `size_t` values are unsigned decimal strings to avoid JSON
number precision loss. Floating-point values are finite JSON numbers. Every
object has an exact field set.

## Moving-obstacle records

Each `MovingObstacleOccupancy` has exactly:

- schema, ID, timeline, workspace-frame, and obstacle IDs;
- algorithm and algorithm version;
- non-negative obstacle padding;
- the original ordered `TimedWorkspaceAabb` trajectory; and
- one ordered `MovingObstacleOccupancySlice` per adjacent waypoint pair.

Each slice binds its ID, source segment index, begin/end ticks, and
outward-rounded padded swept bounds. The slice ID binds the algorithm,
timeline, frame, obstacle, padding, and slice content. The occupancy ID binds
all metadata, waypoints, and slices.

## Report and bundle identities

The report stores:

- its ID, timeline/frame, and complete begin/end ticks;
- status and requested separation margin;
- canonical robot and obstacle occupancy-ID arrays;
- canonical conflict witnesses; and
- exact time-sweep and link-evaluation counts.

Every conflict binds one robot/obstacle occupancy and slice, one robot link,
the positive-duration overlap window, reason, conservative clearance lower
bound, and requested margin. The report ID binds every field except itself.
The bundle ID binds the complete payload except its own ID.

## Loading

The path loader rejects symbolic links and non-regular files and applies the
byte limit before parsing. Path and in-memory byte loaders then:

1. parse exact top-level and nested objects;
2. validate schema and payload SHA-256;
3. enforce robot, obstacle, waypoint, dimension, slice, link-envelope,
   conflict, evaluation, and byte limits;
4. validate strict waypoint ticks and deterministic moving swept bounds;
5. validate every record, slice, report, and bundle identity;
6. require every robot and obstacle to cover the same complete timeline,
   workspace frame, and begin/end ticks; and
7. replay robot-scene analysis under the stored margin and require the exact
   report ID.

Robot-link envelope replay still requires each exact external robot model via
`verify_robot_trajectory_occupancy`.

## Publication

Saving rejects overwrite by default, writes a sibling temporary file, loads
and fully validates the staged bytes under derived limits, and then publishes
the regular file. Explicit overwrite stages the old file and restores it if
publication fails. Symbolic-link destinations are rejected.

SHA-256 provides storage integrity, not publisher authentication, freshness,
rollback resistance, trusted obstacle input, or execution authority.

The fixed Linux/Windows fixture is
[`data/continuous_robot_scene_occupancy_schema1`](../../../data/continuous_robot_scene_occupancy_schema1).
It reproduces bundle ID
`653772769983773f589ae739e4d633ca1224e68b0273dbd3847e3308876e4b3f`
and report ID
`8264e583a0edc29442489f16b2f2217e75a83363c851642641f9ab78aa1d22ce`.
