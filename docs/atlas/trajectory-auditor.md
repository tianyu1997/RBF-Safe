# Trajectory auditor

`TrajectoryAuditor` checks whether an existing `SafeAtlas` continuously covers
a piecewise-linear configuration-space trajectory. It does not sample the
trajectory: each segment is analytically intersected with every certified
C-space AABB, and the resulting parameter intervals are unioned.

Call `atlas.verify_compatible(robot, scene)` before auditing when the Atlas was
loaded from disk. The audit inherits the robot, scene, algorithm, and parameter
identity recorded by the Atlas certificates.

## Report semantics

For waypoints `q[i]` and `q[i+1]`, local segment parameter `u` is in `[0, 1]`
and represents `(1-u) q[i] + u q[i+1]`.

| Status | Meaning |
|---|---|
| `CERTIFIED` | Every `u` on every segment is contained in at least one certified Atlas region. |
| `PARTIAL` | Some trajectory parameters are certified and at least one interval is uncovered. |
| `INVALID` | No certified Atlas region intersects the trajectory. |

`INVALID` means invalid with respect to the supplied safety certificate. It
does not prove collision. Likewise, an uncovered interval is `Unknown`, not an
unsafe or collision interval.

The report contains:

- `coverage_ratio`: the mean covered local-parameter fraction across segments;
- `region_sequence`: intersected region IDs in deterministic first-entry order,
  with consecutive duplicates removed;
- `uncovered_intervals`: a segment index, local start/end fraction, and explicit
  endpoint-inclusion flags for every gap;
- waypoint, segment, and region-test counts.

Each segment has equal weight in `coverage_ratio`; it is not an arc-length,
duration, energy, or risk metric. A zero-length segment is fully covered only
when its stationary configuration belongs to a certified region.

## C++

```cpp
#include <rbfsafe/rbfsafe.h>

std::vector<rbfsafe::Configuration> trajectory{
    {-1.0, 0.0},
    {0.0, 0.0},
    {1.0, 0.0},
};

rbfsafe::TrajectoryAuditOptions options;
options.maximum_region_tests = 1'000'000;
auto report = rbfsafe::TrajectoryAuditor{}.audit(atlas, trajectory, options);
if (!report) {
    // Inspect report.error().code and report.error().describe().
}
```

The trajectory must contain at least two finite, dimension-matching waypoints,
and each coordinate delta must remain finite in double precision.
Budget exhaustion returns `ResourceLimit`; cooperative cancellation returns
`Cancelled`.

## Python and CLI

```python
report = rbfsafe.TrajectoryAuditor().audit(
    atlas,
    [[-1.0, 0.0], [0.0, 0.0], [1.0, 0.0]],
)
print(report.status, report.coverage_ratio)
for interval in report.uncovered_intervals:
    print(interval.segment_index, interval.start_fraction, interval.end_fraction)
```

The CLI accepts either a top-level waypoint array or an object with a
`waypoints` member:

```bash
rbfsafe-inspect atlas --trajectory data/trajectory_2r.json \
  --max-region-tests 10000000
```

CLI gaps use mathematical brackets, for example `0:(0.6,1]` means segment 0,
with the certified boundary at `0.6` excluded from the uncovered interval and
the final waypoint included.

## Limits

- Interpolation is linear in the supplied joint coordinates. Revolute wraparound
  is not inferred; unwrap periodic trajectories before auditing.
- A region sequence is certificate coverage metadata, not a generated path or
  a runtime execution guarantee.
- The auditor does not check timing, velocity, acceleration, torque, tracking
  error, controller behavior, dynamic obstacles, or model calibration.
- The Atlas geometry limitations in the safety model, including the current
  lack of self-collision certification, remain in force.
