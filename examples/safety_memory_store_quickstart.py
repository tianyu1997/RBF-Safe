"""Create and publish immutable revisions in a safety-memory store."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


def digest(value: str) -> str:
    return value * 64


parser = argparse.ArgumentParser()
parser.add_argument("store", type=Path, help="new safety-memory store directory")
args = parser.parse_args()

artifact = rbfsafe.MemoryArtifactInput()
artifact.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
artifact.deployment_id = "arm-a"
artifact.robot_digest = digest("a")
artifact.scene_digest = digest("c")
artifact.task_id = "shelf-pick"
artifact.content_digest = digest("1")
artifact.locator = "artifacts/shelf-atlas"
artifact.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
artifact.tags = ["production", "shelf"]

memory = rbfsafe.SafetyMemory()
registered = memory.register_artifact(artifact)
store = rbfsafe.SafetyMemoryStore.create(args.store, memory)
root = store.current_revision_id
memory.transition(
    registered.id,
    registered.generation,
    rbfsafe.MemoryArtifactState.STALE,
    "scene maintenance",
)
current = store.publish(memory, root)
print(f"root={root}")
print(f"current={current.id}")
print(f"revisions={len(store.revisions)}")
print(f"stale={store.load_current().summary.stale}")
