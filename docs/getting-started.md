# Getting started

This guide builds, saves, loads, and queries a small Atlas. Read the
[safety model](safety-model.md) before interpreting a successful query.

## 1. Define the inputs

RBF-Safe accepts a serial modified-DH robot and an immutable AABB scene. The
repository includes `data/planar_2r.json` and `data/empty_scene.json` as small
examples. See [input formats](input-formats.md) for field-level definitions.

Seeds are configurations of length equal to the robot dimension. They direct
which unresolved LECT branches are refined; they are not evidence.

## 2. Build and query in C++

```cpp
#include <rbfsafe/rbfsafe.h>

#include <iostream>

int main() {
    auto robot = rbfsafe::SerialRobotModel::from_json("data/planar_2r.json");
    auto scene = rbfsafe::SceneSnapshot::from_json("data/empty_scene.json");
    if (!robot || !scene) return 1;

    rbfsafe::BuildOptions options;
    options.maximum_depth = 24;
    options.maximum_nodes = 1'000'000;
    options.minimum_normalized_width = 1e-3;
    options.threads = 1;

    auto result = rbfsafe::AtlasBuilder{}.build(
        robot.value(), scene.value(), {{0.0, 0.0}, {1.0, -1.0}}, options);
    if (!result) {
        std::cerr << result.error().describe() << '\n';
        return 1;
    }

    auto& atlas = result.value().atlas;
    if (!atlas.save("atlas")) return 1;
    std::cout << atlas.contains(rbfsafe::Configuration{0.0, 0.0}) << '\n';

    auto loaded = rbfsafe::SafeAtlas::load("atlas");
    if (!loaded || !loaded.value().verify_compatible(robot.value(), scene.value())) return 1;
    return 0;
}
```

Always call `verify_compatible` when the current robot and scene did not
directly produce the in-memory Atlas.

## 3. Build and query in Python

```python
from pathlib import Path

import rbfsafe

robot = rbfsafe.SerialRobotModel.from_json("data/planar_2r.json")
scene = rbfsafe.SceneSnapshot.from_json("data/empty_scene.json")

options = rbfsafe.BuildOptions()
options.maximum_depth = 24
options.maximum_nodes = 1_000_000
options.threads = 1

result = rbfsafe.AtlasBuilder().build(
    robot, scene, [[0.0, 0.0], [1.0, -1.0]], options
)
result.atlas.save(Path("atlas"))

atlas = rbfsafe.SafeAtlas.load(Path("atlas"))
atlas.verify_compatible(robot, scene)
print(atlas.contains([0.0, 0.0]))
print([region.id for region in atlas.regions_at([0.0, 0.0])])
print(atlas.connected([0.0, 0.0], [1.0, -1.0]))
```

Python maps invalid inputs to `ValueError`, I/O failures to `OSError`, resource
limits to `MemoryError`, and identity/format/corruption/cancellation/internal
failures to subclasses of `RBFSafeError`.

## 4. Inspect and visualize

```bash
rbfsafe-inspect atlas --query 0.0 0.0
rbfsafe-inspect atlas --plot slice.png --dims 0 1
```

The plot is a visualization of stored certified regions, not an independent
certificate verifier.

## 5. Interpret the result

- `contains(q)` means at least one stored `CertifiedRegion` contains `q`.
- `regions_at(q)` returns every matching region.
- `nearest_region(q)` is a geometric nearest-box query and does not certify
  the segment from `q` to that region.
- `route(q1, q2)` returns a deterministic certified path through intersecting
  convex Atlas AABBs; `connected(q1, q2)` is its Boolean shorthand. HiPaC
  remains useful for covering a supplied candidate path with oriented cells.
  Neither API grants a runtime-execution guarantee.
- `false` normally means “not certified by this Atlas,” not “in collision.”

## Build a certified OBB corridor

When a planner or optimizer already produced a candidate polyline, v0.4 can
cover it with certified OBB cells:

```cpp
std::vector<rbfsafe::Configuration> path{
    {-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
auto report = rbfsafe::HipacCorridorBuilder{}.build(robot, scene, path);
if (report && report.value().status == rbfsafe::HipacBuildStatus::Certified) {
    auto route = report.value().corridor.route(path.front(), path.back());
    report.value().corridor.save("corridor");
}
```

The builder recursively splits unresolved segments and returns explicit gaps
for partial coverage. A returned route is a geometric connectivity
certificate through convex cells, not a timing or execution approval. See the
[corridor guide](corridors.md).

## 6. Solve a Safe IK query

Safe IK keeps its search inside certified regions and requires an explicit
Atlas route from the seed to the result:

```python
target = robot.end_effector_pose([0.4, -0.2])
report = rbfsafe.SafeIkSolver().solve(
    robot, scene, atlas, target, [0.0, 0.0]
)
if report.status == rbfsafe.SafeIkStatus.SAFE_CONNECTED:
    print(report.solution)
    print(report.connectivity_route.certificate.id)
```

Pose convergence is point-checked evidence. Read the [Safe IK guide](safe-ik.md)
before using the result in a planning or control system.

## 7. Audit a trajectory

After loading and compatibility-checking an Atlas:

```python
report = rbfsafe.TrajectoryAuditor().audit(
    atlas,
    [[-1.0, 0.0], [0.0, 0.0], [1.0, 0.0]],
)
print(report.status, report.coverage_ratio)
```

Read the [trajectory auditor guide](trajectory-auditor.md) before interpreting
`PARTIAL` or `INVALID`.

## 8. Update a changed scene

Given the exact previous snapshot and a new snapshot:

```python
update = rbfsafe.AtlasUpdater().update(
    robot,
    previous_scene,
    next_scene,
    atlas,
    repair_samples=[[0.0, 0.0]],
)
update.atlas.save("atlas-v2")
print(update.stats.certificates_inherited)
print(update.invalidated_region_ids)
```

For persistent history, create `AtlasVersionStore` from the initial Atlas and
publish each derived version in parent order. Read
[dynamic updates](dynamic-updates.md) before using certificate inheritance or
rollback.

## 9. Register reusable safety memory

After saving an immutable artifact, catalog its exact identities and content
digest:

```python
item = rbfsafe.MemoryArtifactInput()
item.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
item.deployment_id = "arm-a"
item.robot_digest = robot.digest
item.scene_digest = scene.digest
item.task_id = "shelf-pick"
item.content_digest = atlas.version_info.id
item.locator = "artifacts/shelf-atlas"
item.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION

memory = rbfsafe.SafetyMemory()
artifact = memory.register_artifact(item)
memory.save("safety-memory")
```

Cross-task direct reuse still requires exact deployment, robot, and scene
identity. Scene changes must invalidate old records and publish a newly
validated artifact. Read [persistent safety memory](safety-memory.md) before
using the catalog or fleet coordination APIs.

For multiple processes, create a revision store and always publish against the
head you observed:

```python
store = rbfsafe.SafetyMemoryStore.create("safety-memory-store", memory)
expected = store.current_revision_id
memory.invalidate_scene("arm-a", scene.digest, "cell layout changed")
revision = store.publish(memory, expected)
```

A stale `expected` value raises `IdentityMismatchError`; no newer revision is
overwritten. See [transactional safety memory](safety-memory-store.md).

## 10. Authenticate immutable artifact bytes

After registering an artifact, bind its exact bytes and current memory
lifecycle to an attestation. Provision the key through a deployment secret
manager; do not store production keys beside artifacts or attestations.

```python
key = secret_manager_bytes  # 32 to 4096 bytes; not persisted by RBF-Safe
attestation = rbfsafe.attest_artifact_file(
    artifact,
    Path("artifacts/shelf-atlas.bin"),
    "factory-attestation-service",
    "rotation-2026-07",
    key,
    1,
    "application/vnd.rbfsafe.atlas",
)
rbfsafe.save_artifact_attestation(attestation, Path("shelf-atlas.attestation.json"))

loaded = rbfsafe.load_artifact_attestation(Path("shelf-atlas.attestation.json"))
rbfsafe.verify_artifact_file(
    artifact,
    Path("artifacts/shelf-atlas.bin"),
    loaded,
    "factory-attestation-service",
    "rotation-2026-07",
    key,
)
```

HMAC verification proves knowledge of a shared key; it is not a public-key
signature, non-repudiation, execution approval, or new safety evidence. See
[authenticated artifact attestations](artifact-attestation.md).

## 11. Verify a remote artifact exchange

Remote-transfer records use the exact byte SHA-256, so register a dedicated
artifact entry when an existing memory record uses a logical content identity:

```python
import hashlib

payload = Path("artifacts/shelf-atlas.bin").read_bytes()
remote_input = rbfsafe.MemoryArtifactInput()
remote_input.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
remote_input.deployment_id = "arm-a"
remote_input.robot_digest = robot.digest
remote_input.scene_digest = scene.digest
remote_input.task_id = "shelf-pick-transfer"
remote_input.content_digest = hashlib.sha256(payload).hexdigest()
remote_input.locator = "artifacts/shelf-atlas.bin"
remote_input.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
remote_artifact = memory.register_artifact(remote_input)

request = rbfsafe.prepare_artifact_publish(
    memory, remote_artifact.id, payload, "artifact-service", 1,
    "application/vnd.rbfsafe.atlas",
)
# A transport sends request + payload. The service returns and authenticates:
receipt = rbfsafe.make_artifact_publish_receipt(request, 101)
receipt = rbfsafe.authenticate_artifact_publish_receipt(
    receipt, "rotation-2026-07", key
)
verified = rbfsafe.verify_artifact_publish(
    memory, request, receipt, payload, "rotation-2026-07", key
)

journal = rbfsafe.ArtifactTransferJournal()
journal.append(verified, "")
journal.save("artifact-transfer-journal")
```

The HTTP/message-bus/object-store adapter and key provisioning remain outside
RBF-Safe. Read the
[remote artifact service contract](remote-artifact-service.md) before using
this boundary.

## 12. Verify a public-key service offline

Create or load an explicitly authorized public bundle, request Ed25519
authentication, and preserve public verification provenance:

```python
# Reproducible demo seed only; use a CSPRNG plus HSM/secret manager in production.
pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
service_key = rbfsafe.make_service_public_key(
    "artifact-service", pair.public_key, 1, 0,
    rbfsafe.ServiceKeyState.ACTIVE,
    True, True, True,
)
bundle = rbfsafe.ServiceTrustBundle.create(1, "", [service_key])

request = rbfsafe.prepare_artifact_publish(
    memory, remote_artifact.id, payload, "artifact-service", 2,
    "application/vnd.rbfsafe.atlas",
    rbfsafe.ArtifactTransferAuthentication.ED25519,
)
receipt = rbfsafe.make_artifact_publish_receipt(request, 102)
receipt = rbfsafe.sign_artifact_publish_receipt(
    receipt, service_key.id, pair.secret_key
)
verified = rbfsafe.verify_artifact_publish_offline(
    memory, request, receipt, payload, bundle
)
assert verified.trust_bundle_id == bundle.id
```

The application must pin `bundle.id` through an authenticated out-of-band
process; loading an attacker-supplied bundle and trusting its internally
consistent ID is not authentication. See
[public-key service identities](public-service-identities.md).

To authorize and retain a successor, retire or revoke keys monotonically, add
the new public key, sign the exact transition with an active
rotation-capable predecessor, and publish against the retained head:

```python
successor = rbfsafe.rotate_service_trust_bundle(bundle, successor_keys)
authorization = rbfsafe.authorize_service_trust_bundle_successor(
    bundle, successor, service_key.service_id, service_key.id, pair.secret_key
)
history = rbfsafe.ServiceTrustHistory.create(
    "service-trust-history", bundle, trusted_root_id
)
history.publish(successor, authorization, bundle.id)
history = rbfsafe.ServiceTrustHistory.open(
    "service-trust-history", trusted_root_id, successor.id
)
```

Persist `trusted_root_id` through an authenticated out-of-band process and
retain the newest accepted head outside the history rollback domain. The
directory alone cannot distinguish a valid historical copy from a
whole-directory rollback.

For a quorum-governed chain, create a schema-3 root, sign the exact successor
with each independent rotation key, and publish the canonical set:

```python
policy = rbfsafe.ServiceTrustRotationPolicy()
policy.minimum_signatures = 2
policy.require_distinct_services = True
root = rbfsafe.ServiceTrustBundle.create_with_rotation_policy(
    1, "", [service_key, governance_key], policy
)
successor = rbfsafe.rotate_service_trust_bundle(root, successor_keys)
service_authorization = rbfsafe.authorize_service_trust_bundle_successor(
    root, successor, service_key.service_id, service_key.id, service_secret
)
governance_authorization = rbfsafe.authorize_service_trust_bundle_successor(
    root, successor, governance_key.service_id, governance_key.id,
    governance_secret,
)
authorization_set = rbfsafe.assemble_service_trust_bundle_authorizations(
    root, successor, [governance_authorization, service_authorization]
)
history = rbfsafe.ServiceTrustHistory.create(
    "service-trust-history", root, trusted_root_id
)
history.publish(successor, authorization_set, root.id)
```

Export and later verify a portable signed head checkpoint:

```python
history = rbfsafe.ServiceTrustHistory.open(
    "service-trust-history", trusted_root_id, successor.id
)
first = rbfsafe.sign_service_trust_checkpoint(
    history, current_service_key.service_id, current_service_key.id,
    current_service_secret,
)
second = rbfsafe.sign_service_trust_checkpoint(
    history, governance_key.service_id, governance_key.id, governance_secret
)
checkpoint = rbfsafe.assemble_service_trust_checkpoint(
    history, [second, first]
)
checkpoint.save("trust-checkpoint.json")

checkpoint = rbfsafe.ServiceTrustCheckpoint.load("trust-checkpoint.json")
history = rbfsafe.ServiceTrustHistory.open(
    "service-trust-history", trusted_root_id, checkpoint,
    trusted_checkpoint_id,
)
```

Retain `trusted_checkpoint_id` outside the same rollback domain. A valid old
checkpoint does not prove that it is the newest one.

## 13. Create a reviewed deployment profile

Bind deployment assumptions to that exact checkpoint and require independent
Safety and Controls approvals:

```python
constraints = rbfsafe.DeploymentRuntimeConstraints()
constraints.maximum_observation_age_ns = 50_000
constraints.maximum_command_latency_ns = 50_000
constraints.maximum_control_period_ns = 2_000_000
constraints.maximum_consecutive_missed_cycles = 1

review_policy = rbfsafe.DeploymentReviewPolicy()
review_policy.minimum_approvals = 2
review_policy.require_distinct_services = True
review_policy.required_roles = [
    rbfsafe.DeploymentReviewRole.SAFETY,
    rbfsafe.DeploymentReviewRole.CONTROLS,
]

profile_input = rbfsafe.DeploymentProfileInput()
profile_input.deployment_id = "factory-cell-a"
profile_input.robot_digest = trusted_robot_digest
profile_input.controller_digest = trusted_controller_digest
profile_input.platform_digest = trusted_platform_digest
profile_input.runtime_digest = trusted_runtime_digest
profile_input.trust_root_bundle_id = checkpoint.root_bundle_id
profile_input.trust_checkpoint_id = checkpoint.id
profile_input.trust_bundle_id = checkpoint.head_bundle_id
profile_input.trust_bundle_sequence = checkpoint.head_sequence
profile_input.runtime_constraints = constraints
profile_input.review_policy = review_policy
profile = rbfsafe.DeploymentProfile.create(profile_input)

safety = rbfsafe.sign_deployment_profile_approval(
    profile, safety_key.service_id, safety_key.id,
    rbfsafe.DeploymentReviewRole.SAFETY, safety_secret,
)
controls = rbfsafe.sign_deployment_profile_approval(
    profile, controls_key.service_id, controls_key.id,
    rbfsafe.DeploymentReviewRole.CONTROLS, controls_secret,
)
approvals = rbfsafe.assemble_deployment_profile_approvals(
    profile, [controls, safety]
)
reviewed = rbfsafe.ReviewedDeploymentProfile.create(
    profile, approvals, history, checkpoint, trusted_checkpoint_id
)
reviewed.save("reviewed-deployment-profile.json")
```

Each reviewer key must be active, valid at the checkpoint head, and
publication-capable. Loading requires the same caller-pinned trust history and
checkpoint. A conformant runtime assessment remains `Unknown` evidence and
does not authorize execution. See
[reviewed deployment profiles](deployment-profile-format.md).

## 14. Persist fleet-schedule history

Publish canonical reservation reports against the exact memory revision used
to validate their source artifacts:

```python
archive = rbfsafe.FleetScheduleArchive.create(fleet.fleet_id)
root = archive.publish(fleet, memory, reservations, "")
current = archive.publish(fleet, memory, revised_reservations, root.id)
archive.save("fleet-schedules")

loaded = rbfsafe.FleetScheduleArchive.load("fleet-schedules")
loaded.verify_version(current.id, fleet, memory)
```

Preserve that memory revision if the live catalog is later changed. The
archive status remains a declared-envelope coordination result, not an
execution certificate. See [versioned fleet schedules](fleet-schedule-archive.md).

## 15. Apply calibrated policy confidence

Create a profile from reviewed held-out aggregate counts, persist it, then
require the independently configured model and scope identity at use time:

```python
profile = rbfsafe.PolicyCalibrationProfile.load("policy-calibration.json")
options = rbfsafe.CalibratedPolicyGateOptions()
options.minimum_total_samples = 1_000
options.minimum_bin_samples = 30
options.maximum_expected_calibration_error = 0.1
options.maximum_bin_calibration_error = 0.2
options.policy.minimum_confidence = 0.7

report = rbfsafe.CalibratedPolicySafetyGate().check_proposals(
    profile,
    "factory-cell-a",
    trusted_policy_model_digest,
    robot,
    scene,
    atlas,
    current,
    proposals,
    options,
)
```

Inspect `report.applications` to retain both raw and effective confidence.
The nested `policy_report` still requires geometric shield acceptance and
never authorizes execution. See [policy calibration](policy-calibration.md).

For a deployment-facing check, assess a new operational window, record review,
and bind the gate to the exact lifecycle head:

```python
lifecycle = rbfsafe.PolicyCalibrationLifecycle.create(profile)
drift = lifecycle.assess(
    profile,
    operational_window,
    lifecycle.current_event_id,
    rbfsafe.PolicyCalibrationDriftOptions(),
)
assert drift.status == rbfsafe.PolicyCalibrationDriftStatus.STABLE
lifecycle.transition(
    profile,
    lifecycle.current_event_id,
    rbfsafe.PolicyCalibrationLifecycleState.ACTIVE,
    "deployment review approved",
)
trusted_head = lifecycle.current_event_id

report = rbfsafe.CalibratedPolicySafetyGate().check_proposals_guarded(
    profile,
    lifecycle,
    trusted_head,
    "factory-cell-a",
    trusted_policy_model_digest,
    robot,
    scene,
    atlas,
    current,
    proposals,
    options,
)
```

Any new assessment changes the head; drift quarantines the lifecycle and
insufficient data moves an active lifecycle back to pending review. Stable
metrics never reactivate it automatically. Persist and inspect the lifecycle
as described in
[policy calibration drift and lifecycle](policy-calibration-lifecycle.md).

## 16. Verify a bounded execution session

The complete C++ quickstart creates a synthetic Atlas, public trust history,
reviewed profile, command sequence, controller/monitor acknowledgements, and
session in a new directory:

```bash
cmake -S . -B build -DRBFSAFE_BUILD_EXAMPLES=ON
cmake --build build --config Release \
  --target rbfsafe_bounded_execution_session_quickstart
./build/rbfsafe_bounded_execution_session_quickstart session-example
```

To load an existing session, callers must supply every external anchor:

```python
checkpoint = rbfsafe.ServiceTrustCheckpoint.load("checkpoint.json")
history = rbfsafe.ServiceTrustHistory.open(
    "trust-history",
    trusted_root_id,
    checkpoint,
    trusted_checkpoint_id,
)
reviewed = rbfsafe.ReviewedDeploymentProfile.load(
    "profile.json",
    history,
    checkpoint,
    trusted_checkpoint_id,
)
atlas = rbfsafe.SafeAtlas.load("atlas")
session = rbfsafe.BoundedExecutionSession.load(
    "session.json",
    reviewed,
    history,
    checkpoint,
    trusted_checkpoint_id,
    atlas,
)

assert session.evidence == rbfsafe.EvidenceLevel.UNKNOWN
assert not session.authorizes_execution

authorization = session.authorize_command(
    command_index,
    exact_configuration,
    caller_monotonic_dispatch_ns,
)
if authorization is None:
    fail_closed()
assert authorization.evidence == rbfsafe.EvidenceLevel.RUNTIME_EXECUTABLE
assert not authorization.open_ended
```

The observation and dispatch times must use the same trustworthy monotonic
clock domain. The returned value covers only that exact command and closed
window; the application remains responsible for transmission, tracking,
device identity, revocation checks, emergency stops, and independent runtime
monitoring. See
[bounded execution sessions](bounded-execution-session-format.md).

## 17. Record ordered execution authorization

The bounded-session quickstart also creates a completed `ledger` directory.
The Python example replays the same synthetic session into another new ledger:

```bash
python examples/execution_ledger_quickstart.py \
  session-example new-ledger
```

For each command, retain the current record ID and pin the newest accepted
signed trust checkpoint out of band:

```python
ledger = rbfsafe.ExecutionLedger.create("new-ledger", session)
decision = ledger.authorize_command(
    session,
    reviewed,
    current_history,
    current_checkpoint,
    trusted_current_checkpoint_id,
    atlas,
    command.index,
    command.configuration,
    caller_monotonic_dispatch_ns,
    ledger.current_record_id,
)
if decision.authorization is None:
    fail_closed()

completion_input = rbfsafe.ExecutionControllerCompletionInput()
completion_input.outcome = rbfsafe.ExecutionCompletionOutcome.COMPLETED
completion_input.completed_monotonic_ns = caller_monotonic_completion_ns
completion_input.result_digest = caller_result_sha256
completion = rbfsafe.sign_execution_controller_completion(
    session,
    decision.authorization,
    completion_input,
    controller_secret_key,
)
ledger.record_completion(
    session,
    reviewed,
    current_history,
    atlas,
    completion,
    ledger.current_record_id,
)
```

A stale record head, invalid current checkpoint, inactive original reviewer,
duplicate/out-of-order command, or missing controller completion fails closed.
The ledger and `ledger.audit(...)` remain `Unknown` and do not prove physical
execution. See
[revocation-aware execution ledger](execution-ledger-format.md).

## 18. Publish deployment and runtime transparency

The transparency quickstart consumes the bounded-session fixture, creates a
fresh ledger with one outstanding command, obtains two independent
observation signatures, publishes two records to a new log, independently
cosigns both checkpoints, and publishes authenticated gossip:

```bash
python examples/transparency_log_quickstart.py \
  data/bounded_execution_session_schema1 \
  new-observation-ledger \
  new-transparency-log \
  new-gossip-archive
```

Retain the printed namespace, signer service/key/public key, and newest
checkpoint outside the log directory. Reopen only with those exact pins:

```python
identity = rbfsafe.TransparencyLogIdentity.create(
    retained_namespace,
    retained_signer_service_id,
    retained_signer_key_id,
    retained_signer_public_key,
)
log = rbfsafe.TransparencyLog.open(
    "new-transparency-log", identity, retained_checkpoint_id
)
audit = log.audit()
assert audit.verified_records == 2
assert audit.evidence == rbfsafe.EvidenceLevel.UNKNOWN
assert not audit.authorizes_execution()

bundle = retained_history.bundle(retained_gossip_bundle_id)
gossip = rbfsafe.TransparencyGossipArchive.open(
    "new-gossip-archive",
    identity,
    bundle,
    retained_gossip_bundle_id,
    retained_gossip_head,
)
gossip_audit = gossip.audit()
assert gossip_audit.status == rbfsafe.TransparencyGossipStatus.CONSISTENT
assert not gossip_audit.authorizes_execution
```

The example seeds and fixture keys are deterministic test material. Production
systems must use protected keys, authenticated checkpoint distribution,
independent observers, trustworthy monotonic time, and their own gossip
transport/discovery layer. A valid log or witness archive proves retained
software statements, not physical execution. See
[deployment and runtime transparency](transparency-log-format.md) and
[witnessed transparency](witnessed-transparency.md).

## 19. Compare continuous fleet occupancy

Build a conservative swept-link occupancy for each timestamped
piecewise-linear joint trajectory, then compare robots that share the exact
same timeline and workspace frame:

```python
trajectory = [
    rbfsafe.TimedConfiguration(0, [-0.2, 0.1]),
    rbfsafe.TimedConfiguration(32, [0.2, -0.1]),
]
first_frame = rbfsafe.DeploymentFrameBounds()
first_frame.rotation = [0.0, -1.0, 0.0,
                        1.0,  0.0, 0.0,
                        0.0,  0.0, 1.0]
first_frame.translation = [-4.0, 0.0, 0.0]
first_frame.translation_uncertainty = [0.01, 0.01, 0.02]
second_frame = rbfsafe.DeploymentFrameBounds()
second_frame.rotation = first_frame.rotation
second_frame.translation = [4.0, 0.0, 0.0]
second_frame.translation_uncertainty = first_frame.translation_uncertainty

first = rbfsafe.build_robot_trajectory_occupancy_in_frame(
    robot,
    "cell-clock-v1",
    "cell-world",
    "arm-a",
    first_frame,
    trajectory,
)
second = rbfsafe.build_robot_trajectory_occupancy_in_frame(
    robot,
    "cell-clock-v1",
    "cell-world",
    "arm-b",
    second_frame,
    trajectory,
)

options = rbfsafe.ContinuousFleetOccupancyOptions()
options.minimum_separation = 1.0
bundle = rbfsafe.ContinuousFleetOccupancyBundle.create(
    [first, second], options
)
bundle.save("fleet-occupancy.json")
```

Replay each loaded occupancy against its exact robot model before relying on
its stored envelopes. A successful
`CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES` result remains `Unknown` and
non-authorizing: it does not establish obstacle freedom, self-collision
freedom, clock synchronization, moving-frame behavior, controller tracking,
dynamics, or execution.
See [continuous-time fleet occupancy](continuous-fleet-occupancy.md).
