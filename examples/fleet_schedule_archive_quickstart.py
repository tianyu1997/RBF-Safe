"""Publish and persist deterministic fleet-schedule versions."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


def digest(value: str) -> str:
    return value * 64


def box(lower: float, upper: float) -> rbfsafe.WorkspaceAabb:
    return rbfsafe.WorkspaceAabb([lower, -0.1, -0.1], [upper, 0.1, 0.1])


def artifact(deployment: str, robot: str, content: str) -> rbfsafe.MemoryArtifactInput:
    value = rbfsafe.MemoryArtifactInput()
    value.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
    value.deployment_id = deployment
    value.robot_digest = robot
    value.scene_digest = digest("c")
    value.task_id = "coordinated-pick"
    value.content_digest = content
    value.locator = f"artifacts/{deployment}"
    value.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
    return value


parser = argparse.ArgumentParser()
parser.add_argument("archive", type=Path, help="new fleet-schedule archive directory")
args = parser.parse_args()

robot_a = digest("a")
robot_b = digest("b")
memory = rbfsafe.SafetyMemory()
source_a = memory.register_artifact(artifact("arm-a", robot_a, digest("1")))
source_b = memory.register_artifact(artifact("arm-b", robot_b, digest("2")))
fleet = rbfsafe.make_fleet_snapshot(
    "cell-1",
    digest("c"),
    [
        rbfsafe.FleetMember("arm-a", robot_a, box(-2.0, 2.0)),
        rbfsafe.FleetMember("arm-b", robot_b, box(-2.0, 2.0)),
    ],
)
reservation_a = rbfsafe.make_fleet_reservation(
    fleet, memory, "arm-a", source_a.id, box(-1.0, -0.8), 0, 10, 0.05
)
clear_b = rbfsafe.make_fleet_reservation(
    fleet, memory, "arm-b", source_b.id, box(0.8, 1.0), 0, 10, 0.05
)
colliding_b = rbfsafe.make_fleet_reservation(
    fleet, memory, "arm-b", source_b.id, box(-0.9, -0.7), 0, 10
)

archive = rbfsafe.FleetScheduleArchive.create("cell-1")
root = archive.publish(fleet, memory, [clear_b, reservation_a], "")
current = archive.publish(fleet, memory, [reservation_a, colliding_b], root.id)
archive.save(args.archive)
print(f"root={root.id}")
print(f"current={current.id}")
print(f"versions={len(archive.versions)}")
print(f"status={rbfsafe.fleet_schedule_status_name(current.report.status)}")
print(f"conflicts={len(current.report.conflicts)}")
