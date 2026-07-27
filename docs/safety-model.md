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
that policy. The base gate neither derives nor calibrates these values and
cannot verify that a policy or sensor reported them honestly.

The v3.4 calibrated gate can replace raw confidence with the smaller of that
raw score and a held-out bin's 95% Wilson lower bound. It requires exact
profile/model/scope/task identity and explicit sample/error gates. This is an
empirical measurement transform, not a per-proposal correctness proof.
Dataset selection, outcome labels, dependence, distribution shift, and
uncertainty units remain deployment responsibilities. State/action
uncertainty values are named by the profile but are not statistically
recalibrated in 3.4.

The v3.5 monitor compares aggregate operational bin populations and outcomes
with the exact profile baseline, then records an insufficient, stable, or
drift result in an expected-head lifecycle. A guarded gate requires active
state and the latest stable report. These checks can fail closed when declared
metrics cross policy thresholds, but cannot detect arbitrary feature,
perception, causal, environment, or label shift. Active state is reviewed
governance metadata, not a per-action proof or execution authorization.

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

The v3.1 revision store protects publication order and historical integrity,
not authenticity. A memory identity is a deterministic SHA-256 integrity key;
it is not a digital signature, access-control decision, or proof that a remote
artifact service is trusted.

The v3.2 fleet archive protects byte integrity, deterministic schedule
identity, and linear publication order. Replaying a version confirms only the
same declared-envelope analysis under the exact historical memory and fleet;
it neither authenticates those declarations nor upgrades the report to a
certificate.

The v3.3 trust layer authenticates exact payload bytes and memory lifecycle
metadata to holders of one shared HMAC key. It does not validate payload
semantics, elevate evidence, authorize reuse, identify an individual signer,
or protect keys. Metadata inspection without a verify call remains untrusted.

The v3.6 remote layer additionally binds a local request and a service
response/receipt, exact whole-memory identity, lifecycle generation/state,
payload digest/count, media type, and service sequence. Its transfer HMAC
prevents a payload-only proof from being presented as acknowledgement of a
different request. A successful check still proves neither payload semantics
nor execution safety. Explicit `None` authentication detects accidental
corruption only; a journal validates its local identity chain but cannot
independently re-authenticate a service because it intentionally omits
payloads, tags, and keys.

The v3.7 public-identity layer can instead verify that the complete exchange
was signed by an Ed25519 key authorized by the exact caller-supplied trust
bundle, operation permission, state, and service-sequence window. This
separates verification from a shared secret, but does not make an unpinned
bundle trustworthy. Bundle IDs and rotation parents are deterministic
integrity links rather than a certificate authority, transparency proof, or
deployment authorization. `Retired` permits bounded historical verification;
`Revoked` rejects all uses. Neither state changes payload semantics or
evidence level.

The v3.8 trust-history layer additionally verifies that an active,
rotation-capable predecessor key signed the exact successor and that every
bundle/record transition replays to a caller-supplied expected head. This
prevents unauthorized local successors and detects stale/rolled-back copies
when the caller retains a newer head outside the history. It does not
authenticate the original root, detect rollback when the external head is
rolled back with the directory, provide a quorum or transparency service, or
authorize payload use or robot execution.

The v3.9 identity layer can require multiple unique rotation-capable keys and,
optionally, multiple service identities for every exact successor. It can also
export a quorum-signed root/head/record checkpoint and replay a history against
the caller's pinned checkpoint ID. This narrows unauthorized-rotation and
portable-head risks, but a valid old checkpoint remains stale if the caller
does not retain a newer ID. Quorum is organizational authorization metadata,
not a geometric certificate, clock, transparency log, deployment proof, or
execution authorization.

The v3.10 deployment layer can prove that a deterministic manifest was
approved by the required active publication-capable keys under an exact
caller-pinned checkpoint and report whether caller-supplied runtime values
match its declared constraints. It cannot prove that the digests identify the
physical system, that observations are authentic, that a controller follows
commands, or that the checkpoint is newest unless the caller retains that
anchor. Review and conformance remain `Unknown` evidence and never authorize
actuation.

The v3.11 execution layer can prove that one exact certified command sequence
was rebound to that reviewed profile, reapproved by its exact reviewer
identities, accepted by an explicit controller key, and armed by an
independent monitor key carrying a conformant caller-supplied runtime snapshot.
It can issue `RuntimeExecutable` only for one exact command/configuration and
one closed caller-supplied monotonic dispatch window. The session itself
remains `Unknown`.

The v3.12 ledger can additionally prove that exact command authorizations were
issued once, in sequence, under stored signed current checkpoints; that each
next command followed an exact controller-signed completion; and that
caller-reported cancellation, expiration, and dependency revocation events
formed a single append-only expected-head history. It can detect an original
reviewer key becoming inactive in the caller-supplied current trust bundle and
fail closed by recording a terminal revocation. Ledger state and offline audit
remain `Unknown`; only one returned exact command authorization is
`RuntimeExecutable`.

This does not prove that an endpoint key belongs to physical hardware, that
the clock or observation is trustworthy, that a command was transmitted or
executed, that tracking remained inside the certified geometry, or that the
scene/profile/keys were not revoked unless the caller supplies authenticated
current state to the ledger. Those are application and deployment safety
responsibilities.

## Explicit exclusions in v3.12

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
- `contains`, `connected`, Safe IK, MoveIt plugin acceptance, a reviewed
  profile, a bounded session, ledger state, and ledger audit are not
  runtime-execution approvals.
- Planner success, optimizer convergence, and certified sampling do not imply
  timing, dynamic feasibility, tracking accuracy, or `RuntimeExecutable`.
- Shield acceptance, repair, telemetry, on-plan classification, and monotonic
  observation timestamps do not model real-time deadlines or authorize motor
  execution.
- Policy confidence, uncertainty, task/episode identity, observation age, and
  inference latency remain caller assertions. A calibration profile and
  lifecycle provide aggregate held-out and operational evidence for
  confidence only. The drift metrics do not authenticate inference or source
  observations, detect arbitrary distribution shift, recalibrate
  state/action uncertainty, schedule monitoring, identify the reviewer, or
  make persisted feedback an online-learning safety guarantee.
- Memory locators and content digests are caller-provided metadata. The remote
  API requires its opt-in `content_digest` to equal the exact payload SHA-256,
  but neither the memory catalog nor transfer journal fetches, decrypts,
  parses, or semantically revalidates an artifact. Direct `SafetyMemory`
  object mutation still requires in-process serialization;
  `SafetyMemoryStore` serializes publication but does not merge concurrent
  edits or automatically remove a lock left by a crashed writer.
- Artifact HMAC/private keys, trust-root/head/checkpoint distribution and authorization,
  endpoint/redirect policy, TLS, encryption, concrete network I/O, retry
  semantics, credential access, and secret erasure are deployment
  responsibilities. Public-key verification is not a payload certificate,
  legal non-repudiation conclusion, or execution authorization. An explicitly
  unauthenticated transfer is not safe against malicious substitution.
  Service-trust history is a signed local audit chain, not a certificate
  authority, trust-on-first-use protocol, threshold signature, transparency
  log, or remote key-discovery client.
- Deployment-profile identities, reviewer roles, timing bounds, monitor
  status, transport status, and artifact-authentication status are
  caller-governed declarations. A valid signature proves approval of exact
  bytes, not physical correctness, current freshness, hardware attestation,
  controller compliance, or permission to actuate.
- An `ExecutionCommandAuthorization` is valid only for its exact stored
  command bytes and closed monotonic interval. It does not send the command,
  survive clock-domain changes, permit neighboring configurations, authorize
  later commands, or remain valid after a dependency is changed or revoked.
  Authorization evaluation is stateless: it neither proves ordered progress
  nor prevents repeated evaluation or command replay inside the same window.
  Controller/monitor endpoint keys and runtime fields are caller inputs;
  RBF-Safe does not provision devices, attest sensors, read clocks, check
  tracking, enforce real-time scheduling, or operate an emergency stop.
- Fleet member envelopes and reservation occupancy are caller-supplied
  conservative bounds. The v3.2 analyzer and archive do not provide continuous-time
  multi-robot geometry, distributed consensus, clock guarantees, or controller
  interlocks. Archive integrity is not a signature, authorization decision, or
  execution certificate.
- Named release fixtures and benchmark success demonstrate deterministic API
  integration and regression behavior only. They are synthetic, uncalibrated,
  and do not validate a physical robot, workcell, payload, or deployment.

RBF-Safe therefore does not replace emergency stops, independent collision
monitoring, controller limits, calibration checks, or application-specific
risk assessment.
