# Runtime action shield

RBF-Safe 0.9 adds a deterministic, fail-closed action gate for learned
policies, VLA systems, and other proposal generators. Include
`<rbfsafe/shield.h>` and link `RBFSafe::shield`, or use the matching Python
classes. The shield accepts an immutable, robot/scene-compatible `SafeAtlas`
and never calls a fallback collision checker.

## Actions and outcomes

`RuntimeShield::check_action` supports three standard-library action values:

- `JointDeltaAction`: add a joint displacement to the observed state;
- `EndEffectorAction`: solve a target `Pose3d` with connected Safe IK; and
- `TrajectoryAction`: evaluate a piecewise-linear joint trajectory.

Every valid check returns a `ShieldDecision` with exactly one outcome:

- `ACCEPT`: the requested joint or trajectory action is already continuously
  covered by certified regions. An end-effector no-op may also be accepted.
- `REPAIR`: a bounded replacement trajectory was found and independently
  audited as `CERTIFIED`.
- `REJECT`: no replacement satisfying the configured bounds and certified
  connectivity was found. Rejection is an absence-of-certificate result, not
  proof that every possible action is unsafe.

Expected malformed inputs, identity mismatches, exhausted computational
budgets, and cancellation remain `Result<T>` failures rather than policy
decisions.

Each non-rejected decision contains the exact output waypoints, the final
`TrajectoryAuditReport`, a `CertifiedConnectivity` endpoint-route certificate,
the robot and scene digests, and a deterministic decision ID. The ID binds the
input-action digest, decision kind, outcome, reason, requested joint target,
replacement waypoints, connectivity-certificate ID, repair distance, evidence
level, and identities.

## Bounded repair

Joint repair projects the requested target to the nearest certified AABB in a
component containing the current state. Trajectory repair applies the same
projection to every requested waypoint and joins the repaired waypoints with
certified Atlas routes. Ties are resolved by stable region ID.

Repair is accepted only when all of the following hold:

1. the current configuration belongs to the Atlas;
2. every per-waypoint and accumulated joint-space repair distance respects
   `ShieldOptions`;
3. the output waypoint budget is not exceeded;
4. every routed endpoint is certified-connected; and
5. an independent continuous `TrajectoryAuditor` pass reports `CERTIFIED`.

Input/output waypoint counts, repair-region tests, trajectory-audit tests,
Safe IK work, batch actions, and cancellation are independently bounded.

End-effector actions use `SafeIkSolver` with connectivity forced on. A
tolerance-satisfying solution still carries only `PointChecked` pose evidence;
the emitted joint route separately carries geometric connectivity evidence.

```cpp
RuntimeShield shield;
ShieldAction action = JointDeltaAction{{0.1, -0.05}};
auto decision = shield.check_action(robot, scene, atlas, current, action);
if (!decision)
    return handle_error(decision.error());
if (decision.value().outcome == ShieldOutcome::Reject)
    return stop_or_replan();
send_waypoints(decision.value().output_trajectory);
```

```python
shield = rbfsafe.RuntimeShield()
decision = shield.check_joint_action(
    robot, scene, atlas, current, rbfsafe.JointDeltaAction([0.1, -0.05])
)
if decision.outcome is rbfsafe.ShieldOutcome.REJECT:
    stop_or_replan()
else:
    send_waypoints(decision.output_trajectory)
```

## VLA proposal batches and telemetry

`check_actions` checks an ordered proposal batch against one observed state.
The report selects the first `ACCEPT`; if none exists, it selects the first
`REPAIR`; if all are rejected, `selected_index` is empty. Every proposal still
receives its own complete decision, so a caller can audit or train from all
outcomes. Batch size and cancellation are explicit.

`RuntimeShield::telemetry()` returns a synchronized snapshot of action-type,
outcome, repair, waypoint, and batch counters. These counters contain no
configuration values and do not affect deterministic decisions. Wall-clock
latency is intentionally left to the embedding application because it depends
on deployment hardware and scheduling.

## Runtime monitor

`RuntimeShieldMonitor` can be armed only with a compatible non-rejected
decision whose final audit is `CERTIFIED`. Observations require finite
configurations and strictly increasing monotonic timestamps. It reports:

- `ON_CERTIFIED_PLAN` when the state is Atlas-covered and within the configured
  Euclidean joint-space tolerance of the active polyline;
- `CERTIFIED_DEVIATION` when it remains Atlas-covered but is farther away;
- `UNCERTIFIED_STATE` when it leaves all certified regions; and
- `INACTIVE` when no decision is armed.

The monitor classifies observations; it does not control the robot. Tracking
tolerance is a diagnostic threshold, not a verified tracking-error model.
Consequently no shield or monitor output reaches `RuntimeExecutable` in 0.9.

## Integration boundary

The embedding system remains responsible for state-estimation freshness,
joint ordering, interpolation and time parameterization, velocity and
acceleration limits, controller tracking, actuator faults, communication
latency, emergency stops, and changes to the physical scene. A stale Atlas or
an observed `UNCERTIFIED_STATE` must be handled fail-closed by application
policy.
