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

Schema-2 regional certificates also bind their exact C-space AABB. A derived
certificate binds its parent certificate and complete scene transition.

Checksums detect accidental corruption and ordinary tampering; they are not
digital signatures and do not establish publisher authenticity.

## Assumptions

- Modified-DH parameters, joint limits, tool transform, link radii, units, and
  frame conventions accurately and conservatively describe the robot.
- Each represented link is conservatively covered by the straight segment
  between adjacent DH origins expanded by its configured radius.
- Every relevant static environment object is conservatively covered by a
  workspace AABB in the same coordinate frame and length unit.
- The active scene exactly matches the snapshot bound by the selected Atlas
  version between certification/update and use.
- Supported compiler and floating-point modes preserve ordinary IEEE-754
  double behavior; unsafe fast-math transformations are not supported.

## Dynamic update claims

`AtlasUpdater` never accepts scene-digest substitution as proof. It inherits a
region only when the exact prior subject and policy match and every added or
modified obstacle is disjoint from the stored conservative link envelope.
Removed obstacles cannot invalidate freedom. The inherited clearance is
conservatively reduced by distances to new obstacle bounds.

All other regions are directly revalidated. An undetermined result removes the
region and may trigger bounded local refinement. Persisted unresolved domains
permit later recovery when obstacles move away or are removed. Repair samples
guide which unresolved children continue splitting; they do not certify them.

Derived Atlas versions store the complete `SceneDelta`. The version store
checks parent identity, scene endpoints, dependencies, changed-obstacle
separation, certificate parent IDs, and clearance before accepting inherited
claims. Checksums and hashes remain integrity mechanisms, not signatures.

## Trajectory audit claims

`TrajectoryAuditor` certifies only that every parameter value in a
piecewise-linear segment is contained in the union of certified regions. It
computes segment/AABB intersections analytically; waypoint or intermediate
sampling does not establish the result.

`PARTIAL` and `INVALID` are absence-of-certificate results. They do not prove a
collision. Coverage ratio is an equal-segment parameter metric, not a
probability or risk score. Region sequences are not paths, timing plans, or
execution guarantees.

## Planning and OMPL claims

The adapter treats a state as valid only when `SafeAtlas::contains`
returns true. Unknown space is rejected. Its motion validator delegates every
real-vector edge to the continuous trajectory auditor instead of relying on
OMPL's discrete validity-checking resolution. Certified-region sampling only
changes proposal generation; every result remains subject to the installed
state and motion validators.

The v0.8 certified roadmap rechecks exact Atlas intersections and stores each
edge with one covering certified AABB. Importing that roadmap into PRM is an
acceleration hint: the OMPL adapter revalidates every imported vertex and edge.
The high-level planner reports a certified exact solution only after a final
continuous trajectory audit succeeds. Approximate, timed-out, cancelled, and
uncertified results remain explicit non-certificate outcomes.

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

The constraint-sampler allocator draws only from Atlas-certified regions. Its
optional roadmap bias and local jitter still pass through Atlas membership,
MoveIt path-constraint, and group-validity callbacks. Sampling changes search
behavior; it does not replace the response adapter's final audit.

Plugin initialization requires exact joint-variable order, finite joint-limit
agreement, and matching robot/scene/Atlas digests. DH-to-URDF frame and shape
equivalence cannot be derived automatically in v0.5 and remains an explicit
deployment assumption. The plugins therefore issue no `RuntimeExecutable`
evidence.

## Generalized region database claims

The v0.7 database does not raise evidence by converting data. Imported primary
records retain their exact subject-bound `CertifiedRegion` certificates.
Portal records are issued only for a verified point in the complete convex
intersection of two certified AABB/OBB parents. A TrajectoryTube is a certified
cell/Portal chain; it does not add timing or dynamics evidence.

The experimental higher-order validator preserves shared affine variables and
adds interval bounds for every omitted nonlinear term. A successful result is
a regional geometric proof for the represented zonotope or Taylor region.
Membership optimization may conservatively return false when it exhausts its
iteration budget; it cannot create a false certificate because certification
covers the full region independently of membership queries.

## Optimization-consumer claims

The v0.8 optimization layer translates existing certified convex geometry into
linear constraints; it does not issue certificates. AABB, OBB, and Portal
constraints preserve their complete half-space descriptions. Zonotope and
Taylor constraints use explicit bounded auxiliary variables. TrajectoryTube
records must be expanded into their referenced convex cells and are rejected
as a single convex constraint.

TrajOpt, CHOMP, STOMP, and MPC adapters compile the same portable constraint
program. Residuals and bounded projections are integration aids, not proof that
an external optimizer respects the constraints between waypoints. Any emitted
trajectory still requires continuous audit before it can carry geometric
trajectory evidence.

## Runtime-shield claims

The v0.9 shield issues no new geometric certificate. `ACCEPT` means the exact
joint-space output polyline passed the continuous Atlas auditor. `REPAIR`
means the bounded replacement was assembled from certified routes and then
independently audited. `REJECT` means that this bounded procedure found no
eligible output; it does not prove that the proposal is in collision or that
no safe repair exists.

The runtime monitor checks two geometric observations: Atlas membership and
Euclidean joint-space distance to the active certified polyline. Its tracking
tolerance is diagnostic. Timestamps are checked for monotonic ordering but
latency, time synchronization, state-estimation error, controller tracking,
velocity, acceleration, and swept-time collision are not certified. Monitor
outputs therefore remain `Unknown`, `CertifiedRegion`, or
`CertifiedConnectivity`, never `RuntimeExecutable`.

## Learning-policy safety claims

The v2.0 policy gate applies caller-configured confidence, uncertainty,
observation-age, and inference-latency thresholds before invoking the runtime
shield. Passing a threshold means only that the supplied numeric metadata met
that policy. RBF-Safe neither derives nor calibrates these values and cannot
verify that a policy or sensor reported them honestly.

`SelectedAccepted` and `SelectedRepaired` feedback records bind the exact
shield decision and geometric evidence. `EligibleNotSelected`,
`PolicyRejected`, and `ShieldRejected` are deterministic learning/audit labels,
not collision labels. Feedback persistence checks integrity and identity but
does not make a dataset statistically representative, private, authenticated,
or safe for online adaptation. Policy decisions and feedback never carry
`RuntimeExecutable`.

## Safety-memory and fleet claims

The v3.0 memory catalog preserves the evidence level supplied by its source
artifact. Checksums, deterministic IDs, lifecycle replay, and exact reuse
identity protect integrity and compatibility; they do not re-run geometric
validation, authenticate a remote locator, or make old evidence valid in a new
scene. Only active artifacts with exact deployment, robot, and scene identities
are eligible for direct reuse. `RequiresRevalidation` is explicitly not a
safety acceptance.

Fleet reservations require compatible region-certified source metadata, but
their workspace occupancy AABBs are deployment declarations. Schedule analysis
proves only that those declared boxes and requested margins do not conflict in
overlapping logical windows. It does not derive swept link occupancy, clock
synchronization, communication delay, controller behavior, or interactions
with unmodeled objects. `ConflictFreeUnderDeclaredEnvelopes` therefore remains
a coordination status, not `CertifiedConnectivity`, `RuntimeExecutable`, or a
hardware command authorization.

## Explicit exclusions in v3.0

- Robot self-collision is not checked.
- Joint bodies, cables, payloads, or end effectors are covered only if included
  by the supplied link radii and optional tool link.
- Continuous-time dynamic obstacles, swept motion, localization/calibration
  uncertainty, control error, deformation, and latency are not modeled
  automatically. v0.9 updates only between explicit static AABB snapshots.
- AABB separation is the only workspace collision proof; OBB certification
  uses a conservative C-space enclosure rather than a correlated workspace
  proof, and no mesh, KDOP, or swept-time validation is performed.
- Arbitrary AABB/OBB intersection portals are discovered, but zonotope/Taylor
  Portal intersections and continuous-time portals are not.
- Pose tolerances, MoveIt callback acceptance, and trajectory coverage do not
  certify dynamics, controller tracking, or runtime execution.
- `contains`, `connected`, Safe IK, and MoveIt plugin acceptance are not
  runtime-execution approvals.
- Planner success, optimizer convergence, and certified sampling do not imply
  timing, dynamic feasibility, tracking accuracy, or `RuntimeExecutable`.
- Shield acceptance, repair, telemetry, on-plan classification, and monotonic
  observation timestamps do not model real-time deadlines or authorize motor
  execution.
- Policy confidence, uncertainty, task/episode identity, observation age, and
  inference latency are caller assertions. The gate does not authenticate,
  calibrate, or independently measure them, and persisted feedback alone is
  not an online-learning or cross-task safety-memory guarantee.
- Memory locators and content digests are caller-provided metadata. Loading a
  catalog does not fetch, authenticate, decrypt, or revalidate referenced
  artifacts. Multi-process mutation requires external serialization.
- Fleet member envelopes and reservation occupancy are caller-supplied
  conservative bounds. The v3.0 analyzer does not provide continuous-time
  multi-robot geometry, distributed consensus, clock guarantees, or controller
  interlocks.
- Named release fixtures and benchmark success demonstrate deterministic API
  integration and regression behavior only. They are synthetic, uncalibrated,
  and do not validate a physical robot, workcell, payload, or deployment.

RBF-Safe therefore does not replace emergency stops, independent collision
monitoring, controller limits, calibration checks, or application-specific
risk assessment.
