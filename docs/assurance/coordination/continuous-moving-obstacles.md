# Continuous moving-obstacle occupancy

RBF-Safe 4.4 adds deterministic, conservative workspace occupancy for
caller-supplied moving AABBs and compares it with swept robot-link occupancy.
The public CMake target remains `RBFSafe::occupancy`; the high-level API is
also available from Python.

This layer answers whether the stored conservative robot and obstacle swept
envelopes can be separated over one exact logical time window. It is not a
perception, prediction, tracking, clock, collision-certification, control, or
hardware-enforcement system.

## Moving-obstacle input

`build_moving_obstacle_occupancy` requires:

- non-empty timeline, workspace-frame, and stable obstacle identifiers;
- at least two `TimedWorkspaceAabb` waypoints with strictly increasing
  `uint64_t` ticks;
- finite valid lower/upper bounds at every waypoint; and
- explicit waypoint, slice, padding, and cancellation limits.

Lower and upper coordinates are linearly interpolated between adjacent
waypoints. For each segment the builder stores an outward-rounded union of
both endpoint boxes, expanded by the non-negative `obstacle_padding`. That
union contains every linearly interpolated AABB in the segment. Slices use
half-open time intervals `[begin_tick, end_tick)`, while their geometric
bounds include both endpoint boxes.

The builder emits algorithm/version metadata plus deterministic SHA-256 slice
and occupancy IDs. `verify_moving_obstacle_occupancy` reconstructs every
slice and requires the complete identity to match.

The default limits are 100,000 input waypoints and 1,000,000 slices. Padding
defaults to zero. Construction and replay are deterministic and
single-threaded.

## Robot-scene analysis

`analyze_continuous_robot_scene_occupancy` requires at least one valid
`RobotTrajectoryOccupancy` and one valid `MovingObstacleOccupancy`.

All records must bind exactly the same:

- timeline ID;
- workspace-frame ID; and
- first and last ticks.

Every robot deployment ID and obstacle ID must be unique. Requiring complete
window equality prevents an omitted interval from becoming an accidental
separation claim.

For every robot/obstacle pair, a deterministic two-pointer sweep evaluates
temporally overlapping slices. Every robot link AABB is compared with the
obstacle swept AABB. The result is:

- `CertifiedSeparatedUnderSweptEnvelopes` if every overlap is separated by at
  least the requested lower-bound margin; or
- `PotentialConflict` with canonical robot, obstacle, slice, link, tick,
  reason, clearance, and margin witnesses.

An envelope overlap reports `SweptEnvelopeOverlap`. A positive gap smaller
than the requested margin reports `SeparationMarginViolated`. Finite AABB
distance is rounded down before comparison; a non-finite intermediate becomes
a zero lower bound.

Robot count, obstacle count, time-sweep steps, link evaluations, conflicts,
and cancellation have independent limits. Reordering inputs does not change
the report or bundle identity.

## Persistence and replay

`ContinuousRobotSceneOccupancyBundle::create` canonically orders the complete
robot and obstacle records and stores the deterministic report. `save` and
`load` use the independent checksummed schema-1 format documented in
[Continuous robot-scene occupancy format](continuous-robot-scene-occupancy-format.md).

Loading verifies the checksum, exact schema, resource limits, all identities,
complete time coverage, and semantically replays the robot-scene analysis.
It also reconstructs each moving-obstacle envelope from its stored waypoints.
The exact robot models are not stored, so consumers must separately call
`verify_robot_trajectory_occupancy` for every robot record.

## Evidence boundary

Every moving-obstacle occupancy, conflict, report, and bundle returns
`EvidenceLevel::Unknown` and `authorizes_execution() == false`, including a
successful `CertifiedSeparatedUnderSweptEnvelopes` status.

The stored obstacle waypoints are assumptions supplied by the caller. This
release does not establish:

- the authenticity, freshness, accuracy, or completeness of sensing;
- the physical validity of linear AABB interpolation;
- clock synchronization or mapping from logical ticks to execution time;
- obstacle behavior outside the closed stored window;
- robot self-collision or collision with static scene objects;
- velocity, acceleration, dynamics, control tracking, or stop distance;
- uncertainty beyond explicit robot frame bounds, robot link padding, and
  obstacle padding; or
- runtime command or hardware authority.

These obligations must be established by independently reviewed deployment
and runtime systems.

## Examples and inspection

The deterministic examples are:

- [`continuous_robot_scene_occupancy_quickstart.cpp`](../../../examples/coordination/continuous_robot_scene_occupancy_quickstart.cpp)
- [`continuous_robot_scene_occupancy_quickstart.py`](../../../examples/continuous_robot_scene_occupancy_quickstart.py)

Native inspection validates the bundle and replays every obstacle:

```bash
rbfsafe-inspect continuous-robot-scene-occupancy.json
```

The Python CLI can additionally replay all exact robot models and reanalyze a
different margin:

```bash
rbfsafe-inspect continuous-robot-scene-occupancy.json \
  --occupancy-robot arm-a=robot-a.json \
  --occupancy-minimum-separation 0.25
```

If any `--occupancy-robot` argument is supplied, exactly one model is required
for every stored deployment. Unrelated inspector options are rejected.
