"""Create reusable safety memory and check a two-robot reservation schedule."""

from __future__ import annotations

import rbfsafe


def digest(value: str) -> str:
    return value * 64


def box(lower: float, upper: float) -> rbfsafe.WorkspaceAabb:
    return rbfsafe.WorkspaceAabb([lower, -0.1, -0.1], [upper, 0.1, 0.1])


def artifact(deployment: str, robot: str, scene: str, content: str) -> rbfsafe.MemoryArtifactInput:
    value = rbfsafe.MemoryArtifactInput()
    value.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
    value.deployment_id = deployment
    value.robot_digest = robot
    value.scene_digest = scene
    value.task_id = "shelf-pick"
    value.content_digest = content
    value.locator = "artifacts/shelf-atlas"
    value.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
    value.tags = ["production", "shelf"]
    return value


scene = digest("c")
robot_a = digest("a")
robot_b = digest("b")
memory = rbfsafe.SafetyMemory()
source_a = memory.register_artifact(artifact("arm-a", robot_a, scene, digest("1")))
source_b = memory.register_artifact(artifact("arm-b", robot_b, scene, digest("2")))

reuse = rbfsafe.MemoryReuseQuery()
reuse.deployment_id = "arm-a"
reuse.robot_digest = robot_a
reuse.scene_digest = scene
reuse.target_task_id = "shelf-place"
reuse.minimum_evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
reuse.required_tags = ["production"]
candidate = memory.query_reuse(reuse)[0]
memory.record_reuse(candidate.artifact.id, reuse, "shelf-place deployment validation")

fleet = rbfsafe.make_fleet_snapshot(
    "cell-1",
    scene,
    [
        rbfsafe.FleetMember("arm-a", robot_a, box(-2.0, 2.0)),
        rbfsafe.FleetMember("arm-b", robot_b, box(-2.0, 2.0)),
    ],
)
reservation_a = rbfsafe.make_fleet_reservation(
    fleet, memory, "arm-a", source_a.id, box(-1.0, -0.8), 0, 10, 0.05
)
reservation_b = rbfsafe.make_fleet_reservation(
    fleet, memory, "arm-b", source_b.id, box(0.8, 1.0), 0, 10, 0.05
)
schedule = rbfsafe.analyze_fleet_schedule(fleet, memory, [reservation_a, reservation_b])
print(
    f"reuse={rbfsafe.reuse_disposition_name(candidate.disposition)} "
    f"cross_task={str(candidate.cross_task).lower()}"
)
print(
    f"schedule={rbfsafe.fleet_schedule_status_name(schedule.status)} "
    f"conflicts={len(schedule.conflicts)}"
)
