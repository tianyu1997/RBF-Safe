# Safety model

## Certified claim

`CertifiedRegion` means that, for every configuration in one C-space AABB,
each represented link's conservative workspace envelope is disjoint from
every AABB obstacle after the configured padding. Affine arithmetic encloses
endpoint locations; LinkIAABB encloses the segment between paired endpoint
envelopes plus the link radius.

Obstacle and envelope contact counts as overlap, so it cannot produce a
certificate. A positive reported clearance lower bound is measured between
the conservative link AABBs and obstacle AABBs.

## Evidence discipline

An overlap is `Undetermined`, not proof of collision. Atlas construction may
subdivide an undetermined cell, but branches that reach depth, width, node, or
cancellation limits remain uncertified. Samples may prioritize branches and
support regression tests; they never create or upgrade a regional certificate.

Region merging is allowed only when the rectangular union is independently
revalidated. Atlas connectivity is certified only when an explicit chain of
exactly intersecting convex AABBs and witness configurations can be recovered.
It does not produce a timing law, controller command, or execution guarantee.

## Identity and reuse

Certificates bind the canonical robot model, scene snapshot, validator name
and version, obstacle padding, evidence level, and clearance with SHA-256.
Changing any bound input invalidates direct reuse. Loaded Atlases must be
checked with `verify_compatible` against the current robot and scene.

Checksums detect accidental corruption and ordinary tampering; they are not
digital signatures and do not establish publisher authenticity.

## Assumptions

- Modified-DH parameters, joint limits, tool transform, link radii, units, and
  frame conventions accurately and conservatively describe the robot.
- Each represented link is conservatively covered by the straight segment
  between adjacent DH origins expanded by its configured radius.
- Every relevant static environment object is conservatively covered by a
  workspace AABB in the same coordinate frame and length unit.
- The scene does not change between certification and use.
- Supported compiler and floating-point modes preserve ordinary IEEE-754
  double behavior; unsafe fast-math transformations are not supported.

## Trajectory audit claims

`TrajectoryAuditor` certifies only that every parameter value in a
piecewise-linear segment is contained in the union of certified regions. It
computes segment/AABB intersections analytically; waypoint or intermediate
sampling does not establish the result.

`PARTIAL` and `INVALID` are absence-of-certificate results. They do not prove a
collision. Coverage ratio is an equal-segment parameter metric, not a
probability or risk score. Region sequences are not paths, timing plans, or
execution guarantees.

## OMPL adapter claims

The v0.3 adapter treats a state as valid only when `SafeAtlas::contains`
returns true. Unknown space is rejected. Its motion validator delegates every
real-vector edge to the continuous trajectory auditor instead of relying on
OMPL's discrete validity-checking resolution. Certified-region sampling only
changes proposal generation; every result remains subject to the installed
state and motion validators.

The adapter does not turn an Atlas into an execution certificate, restore
probabilistic completeness outside the certified union, or certify a planner's
termination and optimization behavior. A final returned path should still be
audited before downstream use.

## OBB corridor and connectivity claims

A v0.4 OBB is certified only through its enclosing C-space AABB. The enclosure
is a superset of the rotated cell, so a successful IFK-AA/LinkIAABB proof is
valid for the OBB. Failure to certify the enclosure says nothing conclusive
about the exact OBB or collision state.

A witness portal contains one configuration that belongs to both adjacent
certified convex OBBs. This proves that their union is path-connected. A
`CertifiedRoute` connects query points to consecutive portal witnesses with
line segments inside those convex cells and binds the exact sequence and
waypoints through `Certificate::subject_digest`.

`CertifiedConnectivity` remains strictly below `RuntimeExecutable`. It does
not cover velocity, acceleration, timing, actuator limits, tracking error,
control discretization, dynamic obstacles, or unmodeled robot geometry.

## Safe IK claims

The v0.5 solver projects every numerical iterate into one Atlas
`CertifiedRegion`. A returned pose match is still only `PointChecked`: the
finite-difference numerical solve does not prove pose satisfaction over a
neighborhood. The destination region certificate separately proves geometric
collision freedom for every configuration in that region under the recorded
robot and scene.

`SafeConnected` additionally includes an Atlas route from the supplied current
configuration to the solution. The route certificate binds the exact region
sequence and intersection witnesses. `SafeUnconnected`, `SeedNotCertified`,
and `NoSolution` are absence-of-certificate outcomes; none proves that no safe
IK solution exists.

## MoveIt 2 integration claims

The request adapter checks only that the planning start is Atlas-covered. The
response adapter is the enforcement point: it clears any successful response
unless the entire piecewise-linear joint trajectory audits as `CERTIFIED`.
The kinematics plugin returns only `SafeConnected` solutions.

Plugin initialization requires exact joint-variable order, finite joint-limit
agreement, and matching robot/scene/Atlas digests. DH-to-URDF frame and shape
equivalence cannot be derived automatically in v0.5 and remains an explicit
deployment assumption. The plugins therefore issue no `RuntimeExecutable`
evidence.

## Explicit exclusions in v0.5

- Robot self-collision is not checked.
- Joint bodies, cables, payloads, or end effectors are covered only if included
  by the supplied link radii and optional tool link.
- Dynamic obstacles, localization/calibration uncertainty, control error,
  deformation, and latency are not modeled automatically.
- AABB separation is the only workspace collision proof; OBB certification
  uses a conservative C-space enclosure rather than a correlated workspace
  proof, and no mesh, KDOP, or swept-time validation is performed.
- Arbitrary OBB intersection portals are not discovered; portals are
  shared witnesses between consecutive path-cover cells.
- Pose tolerances, MoveIt callback acceptance, and trajectory coverage do not
  certify dynamics, controller tracking, or runtime execution.
- `contains`, `connected`, Safe IK, and MoveIt plugin acceptance are not
  runtime-execution approvals.

RBF-Safe therefore does not replace emergency stops, independent collision
monitoring, controller limits, calibration checks, or application-specific
risk assessment.
