# Continuous-time fleet occupancy

RBF-Safe 4.1 derives conservative per-link workspace occupancies from
timestamped, piecewise-linear joint trajectories and checks robot pairs over
one explicit logical timeline. The public CMake target is
`RBFSafe::occupancy`; the same high-level values and operations are available
from Python.

This layer closes the geometric gap left by the v3 fleet scheduler, whose
reservation AABB is supplied by the caller. It does not replace the scheduler,
an Atlas, obstacle checking, a controller interlock, or physical tracking
monitoring.

## Input contract

`build_robot_trajectory_occupancy_in_frame` requires:

- one valid `SerialRobotModel`;
- at least two `TimedConfiguration` values with strictly increasing
  `uint64_t` ticks;
- configurations inside the model's joint limits;
- non-empty caller-defined timeline, workspace-frame, and deployment IDs;
- a valid `DeploymentFrameBounds` from the robot-local DH base into the
  workspace frame; and
- explicit subdivision, padding, work, and cancellation limits.

The trajectory between adjacent waypoints is linear in the supplied joint
coordinates. Ticks are abstract values in a caller-owned clock domain. Slices
use half-open time intervals `[begin_tick, end_tick)`. The conservative joint
box still includes both endpoint configurations, so two trajectories that
overlap before a common end tick include that endpoint geometry in their
compared envelopes. A deployment that ends exactly when another starts has no
overlapping interval; the caller owns that hand-off convention.

The workspace-frame IDs must be exactly equal before occupancies can be
compared. `DeploymentFrameBounds` contains a row-major right-handed
orthonormal rotation, a nominal translation, independent non-negative
translation half-widths on the three workspace axes, and a geodesic angular
uncertainty bound in `[0, pi]`. The angle bounds an arbitrary-axis rotation
error around the robot base origin. It is not a covariance or probability.
Moving frames and time-varying uncertainty are not supported.

The preserved `build_robot_trajectory_occupancy` overload accepts only a
translation and emits the original schema-1/version-1 semantics. New code
should use `build_robot_trajectory_occupancy_in_frame`, which emits
schema-2/version-2 records even when its frame is exact.

## Conservative construction

For every original trajectory segment, the builder:

1. forms the axis-aligned joint box containing its endpoint configurations;
2. bisects the logical time interval when its largest normalized joint width
   exceeds the configured limit, subject to depth and integer-tick bounds;
3. evaluates every midpoint directly from the original piecewise-linear
   segment, making the subdivision independent of traversal history;
4. calls the existing `IFK-AA` link-envelope kernel on every leaf joint box;
5. maps each local link AABB through the nominal rotation and translation
   using outward-rounded interval arithmetic;
6. expands every workspace axis by its translation uncertainty and by
   `min(alpha, 2) r`, where `alpha` is the angular uncertainty and `r` is an
   outward upper bound on the nominally transformed AABB's maximum distance
   from the base origin; this upper-bounds the exact rotational chord
   displacement without relying on transcendental-library rounding; and
7. assigns deterministic SHA-256 slice and occupancy identities.

The `IFK-AA` envelope contains each represented link for every configuration
in the leaf joint box. Because the piecewise-linear subsegment lies in that
box, its complete link sweep is contained by the persisted per-link AABBs.
Subdivision improves tightness; reaching a depth or tick limit does not make
the result unsound because the unsplit joint box remains conservative.

The builder is deterministic and single-threaded in 4.1. It validates
waypoint, slice, link-envelope, and cancellation limits before or during
work. Defaults are 100,000 waypoints, 1,000,000 slices, 10,000,000 link
envelopes, subdivision depth 16, normalized joint width `0.05`, and zero
extra padding.

## Fleet analysis

`analyze_continuous_fleet_occupancy` requires at least two occupancies, then
validates and canonically orders them. Deployment IDs must be unique and all
records must bind the same timeline and workspace frame. For every pair of
slices with overlapping half-open time intervals, it compares every pair of
link AABBs. A deterministic two-pointer time sweep makes non-overlapping
slice ranges linear in their stored sizes. Every sweep step and every link
pair in an actual overlap is charged to an explicit independent budget.

The result is:

- `CertifiedSeparatedUnderSweptEnvelopes` when every evaluated link-pair AABB
  has at least the requested lower-bound separation; or
- `PotentialConflict` with deterministic link/slice witnesses when two
  envelopes overlap or their lower-bound distance is below the margin.

Finite AABB distance results are rounded down before comparison. A
non-finite distance intermediate is treated as a zero lower bound, so numeric
overflow cannot create a separation claim.

An overlap is conservative: it means the AABB proof cannot separate the
links, not that the physical links necessarily collide. The successful
status is a separation statement only under the exact stored swept envelopes,
timeline, bounded deployment frames, robot models, piecewise-linear
interpolation, and padding assumptions.

Two records with disjoint active time ranges have no concurrent occupancy and
therefore produce a vacuous separation result with zero link-pair
evaluations; the time-sweep steps are still counted. That result makes no
claim about either robot outside its stored time range.

Time-sweep steps, link-pair evaluations, and stored conflict witnesses have
independent hard limits. Input ordering does not change occupancy IDs, the
report ID, conflict order, or the bundle ID.

## Replay and evidence boundary

Call `verify_robot_trajectory_occupancy` with the exact robot model before
consuming a loaded occupancy. Replay recomputes every joint subdivision and
IFK-AA envelope and requires the resulting occupancy identity to match.
`ContinuousFleetOccupancyBundle::load` checks storage integrity, record
identities, complete time coverage, resource bounds, and replays the fleet
comparison, but it cannot reconstruct an absent robot model.

Every occupancy, conflict witness, report, bundle, and successful separation
status has `EvidenceLevel::Unknown` and
`authorizes_execution() == false`. In particular, this layer:

- does not check static/dynamic obstacles or robot self-collision;
- does not certify velocity, acceleration, torque, timing accuracy, or
  controller tracking;
- does not read a clock, synchronize robots, reserve resources, transmit
  commands, or stop hardware;
- does not include payloads, cables, tools, deformation, tracking error, or
  uncertainty beyond the model's link radii, tool link, padding, and explicit
  frame bounds; and
- does not combine its result with Atlas, reviewed-profile, execution-ledger,
  provenance, or runtime-monitor evidence automatically.

Applications must separately establish obstacle-free motion and enforce
current execution, tracking, clock, emergency-stop, and deployment
requirements.

## C++ example

```cpp
std::vector<TimedConfiguration> path{{0, {-0.2, 0.1}},
                                     {32, {0.2, -0.1}}};
DeploymentFrameBounds first_frame;
first_frame.rotation = {0.0, -1.0, 0.0,
                        1.0,  0.0, 0.0,
                        0.0,  0.0, 1.0};
first_frame.translation = {-4.0, 0.0, 0.0};
first_frame.translation_uncertainty = {0.01, 0.01, 0.02};
auto second_frame = first_frame;
second_frame.translation = {4.0, 0.0, 0.0};
auto first = build_robot_trajectory_occupancy_in_frame(
    robot, "cell-clock-v1", "cell-world", "arm-a", first_frame, path);
auto second = build_robot_trajectory_occupancy_in_frame(
    robot, "cell-clock-v1", "cell-world", "arm-b", second_frame, path);

ContinuousFleetOccupancyOptions options;
options.minimum_separation = 1.0;
auto bundle = ContinuousFleetOccupancyBundle::create(
    {first.value(), second.value()}, options);
bundle.value().save("fleet-occupancy.json");
```

The complete deterministic example is
[`examples/coordination/continuous_occupancy_quickstart.cpp`](../../../examples/coordination/continuous_occupancy_quickstart.cpp).
The equivalent high-level Python example is
[`examples/continuous_occupancy_quickstart.py`](../../../examples/continuous_occupancy_quickstart.py).

## Inspection

Native inspection validates the persisted bundle and replays its stored fleet
analysis:

```bash
rbfsafe-inspect fleet-occupancy.json
```

The Python CLI can additionally replay every robot occupancy and evaluate a
different separation margin:

```bash
rbfsafe-inspect fleet-occupancy.json \
  --occupancy-robot arm-a=robot-a.json \
  --occupancy-robot arm-b=robot-b.json \
  --occupancy-minimum-separation 0.25
```

When any `--occupancy-robot` argument is supplied, exactly one model is
required for every deployment.
