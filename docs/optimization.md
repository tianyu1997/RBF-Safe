# Certified-region optimization adapters

RBF-Safe 0.8 exposes solver-neutral convex constraints for trajectory
optimization. Include `<rbfsafe/optimization.h>` and link
`RBFSafe::optimization`.

## Linear constraint representation

`compile_region_constraint(database, region_id)` returns a
`LinearRegionConstraint` over variables `x = [q, z]`:

```text
A x <= b
E x  = f
lower_z <= z <= upper_z
```

The exact compilation is:

| Region | Representation |
|---|---|
| AABB | two inequalities per joint |
| OBB | two inequalities per orthonormal local axis |
| Portal | its complete stored half-space intersection |
| Zonotope | `q - G z = center`, `z in [-1,1]` |
| Taylor | linear generators plus one independent bounded remainder variable for every nonzero joint remainder |

`TrajectoryTube` is a union/sequence rather than one convex set and is
rejected. Expand its referenced cells before compiling it.

Every compiled constraint retains the region and certificate IDs and checks
the expected `CertifiedRegion` or `CertifiedConnectivity` evidence. Conversion
does not issue or upgrade a certificate.

## Trajectory programs

`assign_trajectory_regions` deterministically chooses the smallest-ID primary
region containing each waypoint under explicit waypoint and region-test
budgets. Its result is `Complete`, `Partial`, or `Invalid`. This assignment
only matches waypoints; it does not certify interpolation between them.

`compile_trajectory_constraints` and the named `TrajOptRegionAdapter`,
`ChompRegionAdapter`, `StompRegionAdapter`, and `MpcRegionAdapter` create one
constraint stage per waypoint. The named adapters share the same mathematical
program and label the intended consumer; they do not link or vendor any of
those external solvers.

`evaluate_trajectory_constraints` returns maximum violation, a squared hinge
and equality penalty, and gradients with respect to both `q` and lifted
variables. CHOMP and STOMP consumers can use these as costs. TrajOpt and MPC
consumers can import the dense rows directly.

`project` and `project_trajectory_constraints` use bounded cyclic projection
over half-spaces, equalities, and auxiliary boxes. `converged=false` is a
normal bounded-search outcome. A converged numerical projection is still not
an execution or trajectory certificate.

## Required final audit

After any optimizer changes a trajectory, run `TrajectoryAuditor` against the
compatible `SafeAtlas`. Only a fully `Certified` report establishes continuous
piecewise-linear coverage. Optimizer convergence, zero constraint residual at
waypoints, smoothness, and low collision cost do not substitute for this
audit.

## Python example

```python
assignment = rbfsafe.assign_trajectory_regions(database, trajectory)
if assignment.status is rbfsafe.TrajectoryAssignmentStatus.COMPLETE:
    program = rbfsafe.TrajOptRegionAdapter().compile(
        database, assignment.region_ids
    )
    residual = rbfsafe.evaluate_trajectory_constraints(program, trajectory)
```
