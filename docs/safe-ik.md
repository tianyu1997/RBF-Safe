# Safe IK

Safe IK finds an end-effector pose solution while keeping every numerical
iterate inside one certified Atlas region. It also attempts to recover an
explicit certified route from the supplied current configuration to the
solution.

## Evidence returned

A `SafeIkReport` deliberately separates three different claims:

1. `pose_evidence` is `PointChecked` when the returned configuration satisfies
   the configured position and orientation tolerances.
2. `region_certificate` is the existing `CertifiedRegion` certificate for the
   region containing the solution.
3. `connectivity_route`, when present, carries a `CertifiedConnectivity`
   certificate bound to the exact AABB chain and witness waypoints from the
   current state to the solution.

Numerical convergence never upgrades a point check into a regional pose claim.
No Safe IK result is `RuntimeExecutable` evidence.

## C++ API

Include `<rbfsafe/safe_ik.h>` and link `RBFSafe::ik`:

```cpp
auto target = robot.end_effector_pose(rbfsafe::Configuration{0.4, -0.2});
auto report = rbfsafe::SafeIkSolver{}.solve(
    robot, scene, atlas, target.value(), rbfsafe::Configuration{0.0, 0.0});
if (!report) {
    std::cerr << report.error().describe() << '\n';
    return 1;
}
if (report.value().status == rbfsafe::SafeIkStatus::SafeConnected) {
    use_for_further_certified_planning(report.value().solution);
}
```

`Pose3d::orientation` uses quaternion order `x, y, z, w`. Input quaternions
must be finite and unit length within the validation tolerance.

## Search behavior

The solver first validates the robot, scene, Atlas identity, target, seed, and
all numerical options. It then:

1. rejects a seed outside all certified regions;
2. orders candidate regions by distance from the seed and stable region ID;
3. starts deterministic projected damped-least-squares searches in each
   bounded region;
4. accepts only a configuration satisfying both pose tolerances; and
5. tries to recover an exact-intersection Atlas route from seed to result.

Every iterate and line-search candidate is clamped to the active certified
AABB. Finite differences are also evaluated inside that box. Region attempts,
iterations, pose evaluations, and disconnected solutions are reported.

The four status values are:

| Status | Meaning |
|---|---|
| `SafeConnected` | A pose solution and certified Atlas route were recovered |
| `SafeUnconnected` | A pose solution is region-certified but no route from the seed exists |
| `SeedNotCertified` | The seed is outside the Atlas |
| `NoSolution` | The bounded deterministic search found no pose solution |

The last three are not collision or infeasibility proofs. With
`require_connectivity=true` (the default), the solver continues after a
disconnected solution in case a connected one exists, but preserves the best
disconnected result for diagnosis.

## Python and CLI

The Python types mirror the high-level C++ API:

```python
target = robot.end_effector_pose([0.4, -0.2])
report = rbfsafe.SafeIkSolver().solve(robot, scene, atlas, target, [0.0, 0.0])
assert report.status == rbfsafe.SafeIkStatus.SAFE_CONNECTED
print(report.connectivity_route.certificate.id)
```

An existing Atlas can be queried from the command line:

```bash
rbfsafe-inspect atlas \
  --robot data/planar_2r.json \
  --scene data/empty_scene.json \
  --ik-target X Y Z QX QY QZ QW \
  --seed Q0 Q1
```

The command returns exit status 3 when the result is not `SAFE_CONNECTED`.

## Limits

v0.5 supports one serial DH chain and one end-effector pose. It does not solve
multi-tip, closed-chain, inequality, self-collision, dynamic, velocity,
acceleration, or controller feasibility constraints. Search completeness is
not claimed. Increase budgets only after checking the modeling assumptions in
the [safety model](safety-model.md).
