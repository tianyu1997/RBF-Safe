# Continuous fleet occupancy bundle schema 1

`ContinuousFleetOccupancyBundle` uses one checksummed JSON file. It is
independent from Atlas, LECT, corridor, region-database, fleet-schedule, and
execution schemas.

## Document

The top-level object has exactly these fields:

```json
{
  "checksum": "<sha256 of compact canonical payload JSON>",
  "format": "rbfsafe-continuous-fleet-occupancy-bundle",
  "library_version": "4.0.0",
  "payload": {},
  "schema": 1
}
```

`library_version` is informational. `schema` controls decoding. Unknown
formats or schemas return `IncompatibleFormat`; malformed, truncated, or
checksum-invalid files return `CorruptData` (or the applicable bounded I/O
error).

The payload stores:

- bundle storage schema and deterministic bundle ID;
- canonically ordered complete `RobotTrajectoryOccupancy` records; and
- the complete replayable `ContinuousFleetOccupancyReport`.

All `uint64_t` and `size_t` values are unsigned decimal strings to avoid JSON
number precision loss. Floating-point coordinates and bounds are finite JSON
numbers. Objects use exact field sets; arrays and aggregate allocations are
checked against caller-configurable limits before reserve or replay.

Each occupancy stores its robot digest, deployment/timeline/frame identities,
fixed translation, algorithm/version and construction parameters, original
timed configurations, complete ordered slice coverage, C-space domains, and
per-link swept AABBs. Each report stores canonical occupancy IDs, separation
margin, status, deterministic conflict witnesses, and exact evaluation
counts.

## Identities and validation

Slice identities bind the algorithm, robot, deployment, timeline, frame, time
range, source segment, joint domain, and link envelopes. The occupancy ID
binds all metadata, waypoints, and slices. The report ID binds all inputs,
outcomes, conflicts, and evaluation counts. The bundle ID binds the complete
payload except its own ID.

Loading:

1. rejects symbolic links and non-regular files;
2. enforces the byte limit before allocation;
3. parses the exact top-level and nested schema;
4. verifies the payload checksum;
5. enforces dimension, waypoint, slice, envelope, occupancy, conflict, and
   evaluation limits;
6. verifies strict waypoint ticks and complete gap-free, non-overlapping
   slice coverage of every piecewise-linear segment;
7. verifies every deterministic identity and canonical order; and
8. replays fleet pair analysis under the stored margin and compares the exact
   report identity.

The loader cannot prove stored IFK-AA link envelopes without the exact robot
models. Consumers must separately call
`verify_robot_trajectory_occupancy` for every loaded occupancy.

## Publication

Saving rejects overwrite by default. The writer emits a sibling temporary
file, loads and fully validates that staged file under limits derived from the
in-memory bundle, and only then publishes it. Requested overwrite stages the
old regular file and restores it if publication fails. Symbolic-link
destinations are rejected.

SHA-256 detects accidental or unkeyed content changes; it is not an
authentication signature. Use the separately governed artifact/trust layers
when publisher authenticity or rollback resistance is required.

The committed fixture is
[`data/continuous_fleet_occupancy_schema1`](../data/continuous_fleet_occupancy_schema1).
It must remain byte-readable on Linux and Windows and must reproduce bundle
ID `d9a6a28c80ae86a28b996c8da954c33c725d9883a22f9f080f22d51e72be4231`.
