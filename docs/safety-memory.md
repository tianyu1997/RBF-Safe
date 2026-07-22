# Persistent safety memory and fleet coordination

RBF-Safe 3.0 adds `RBFSafe::memory`, a persistent catalog for safety artifacts
produced by the geometric, planning, audit, shield, and learning layers. It is
an identity and lifecycle layer: it does not reinterpret bytes from another
format and it never upgrades the evidence stored by an artifact.

## Artifact model

`MemoryArtifactInput` records:

- a deployment ID for the physical/logical robot instance;
- exact robot and scene SHA-256 identities;
- the producing task and artifact type;
- a content SHA-256, opaque locator, evidence level, and sorted tags.

`SafetyMemory::register_artifact` validates the record, normalizes tags, assigns
a deterministic artifact ID, and appends a deterministic audit event. Repeating
the same registration is idempotent. The locator is metadata only; loading a
safety memory never opens or trusts the referenced artifact.

Artifacts have monotonic lifecycle states:

```text
Active -> Stale -> Quarantined -> Retired
   |         |                         ^
   +---------+-------------------------+
```

Allowed transitions are intentionally one-way. Revalidation creates and
registers a new artifact instead of resurrecting old evidence. Transitions use
an expected generation, providing optimistic-concurrency protection for
industrial controllers and deployment services.

`invalidate_scene(deployment_id, scene_digest, reason)` atomically marks every
matching active artifact stale and records one `SceneInvalidated` event per
artifact. It complements `AtlasUpdater`: update or rebuild the affected Atlas,
save it as a new immutable artifact, then register the new digest.

## Cross-task reuse

`MemoryReuseQuery` separates direct reuse from discovery of useful but stale
material. Direct reuse requires all of the following:

- the deployment ID, robot digest, and scene digest match exactly;
- the artifact is active;
- its type, evidence, and required tags satisfy the query; and
- same-task reuse is permitted or the source task differs from the target.

A robot match with a stale artifact or different scene is reported as
`RequiresRevalidation`. Robot/deployment mismatches, quarantined or retired
artifacts, insufficient evidence, and type/tag mismatches are `Ineligible`.
Nothing in this classification changes a `Certificate` or creates new safety
evidence. `record_reuse` accepts only a `Direct` decision and appends the target
task and operator-supplied audit detail.

```cpp
MemoryReuseQuery query;
query.deployment_id = "arm-a";
query.robot_digest = robot.digest();
query.scene_digest = scene.digest();
query.target_task_id = "shelf-place";
query.minimum_evidence = EvidenceLevel::CertifiedRegion;
query.required_tags = {"production"};

auto candidates = memory.query_reuse(query);
if (candidates && !candidates.value().empty()) {
    memory.record_reuse(candidates.value().front().artifact.id, query,
                        "deployment run 17");
}
```

## Multi-robot coordination

`FleetSnapshot` binds a fleet ID, one exact scene digest, and a sorted set of
deployment IDs, robot digests, and declared operating envelopes.
`make_fleet_reservation` accepts only an active source artifact for that exact
fleet member and scene, with at least `CertifiedRegion` evidence and an
appropriate geometric/audit artifact type. The reservation occupancy must lie
inside the member operating envelope.

`analyze_fleet_schedule` rechecks every source against the current
`SafetyMemory`, then compares reservations whose half-open logical time windows
overlap. A source that became stale after reservation creation fails closed.
The analyzer reports:

- duplicate time assignments for one robot;
- overlapping declared workspace AABBs; and
- lower-bound separation below either reservation's requested margin.

Inputs and output order are canonical, work is budgeted, and cancellation is
supported. A successful status is deliberately named
`ConflictFreeUnderDeclaredEnvelopes`: RBF-Safe does not independently derive
the reservation occupancy from link motion in v3.0. The report is not a
`Certificate`, does not claim continuous-time dynamic collision avoidance, and
never produces `RuntimeExecutable` evidence. Deployments must conservatively
produce the occupancy envelopes and retain their source artifacts.

## Industrial deployment pattern

1. Persist Atlas/corridor/audit artifacts in their own checksummed formats.
2. Compute the stored artifact's content digest and register its relative or
   service locator in `SafetyMemory`.
3. Query direct reuse using the live robot and scene identities.
4. Record every accepted reuse and lifecycle transition with a meaningful
   audit detail.
5. On a scene change, invalidate the old identity before rebuilding or
   publishing a replacement.
6. Use fleet reports as a conservative coordination guard in addition to,
   never instead of, robot-level runtime monitoring and hardware safety.

The database is not internally synchronized. A process should serialize
mutations or use generation checks around a load-modify-save transaction.
Writers publish a complete new directory atomically and refuse overwrite by
default. See [the schema-1 format](safety-memory-format.md).

Runnable examples are
[`examples/safety_memory_quickstart.cpp`](../examples/safety_memory_quickstart.cpp)
and
[`examples/safety_memory_quickstart.py`](../examples/safety_memory_quickstart.py).
