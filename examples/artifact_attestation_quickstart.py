"""Create and verify a symmetric artifact attestation sidecar."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


def digest(value: str) -> str:
    return value * 64


parser = argparse.ArgumentParser()
parser.add_argument("payload", type=Path, help="existing immutable artifact payload")
parser.add_argument("attestation", type=Path, help="new attestation JSON file")
args = parser.parse_args()

item = rbfsafe.MemoryArtifactInput()
item.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
item.deployment_id = "arm-a"
item.robot_digest = digest("a")
item.scene_digest = digest("b")
item.task_id = "shelf-pick"
item.content_digest = digest("c")
item.locator = "artifacts/shelf-atlas"
item.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
memory = rbfsafe.SafetyMemory()
artifact = memory.register_artifact(item)

# Demonstration key only. Production keys must come from an external secret manager.
key = bytes(range(1, 33))
attestation = rbfsafe.attest_artifact_file(
    artifact,
    args.payload,
    "factory-service",
    "example-key-1",
    key,
    1,
    "application/vnd.rbfsafe.atlas",
)
rbfsafe.save_artifact_attestation(attestation, args.attestation)
loaded = rbfsafe.load_artifact_attestation(args.attestation)
rbfsafe.verify_artifact_file(
    artifact,
    args.payload,
    loaded,
    "factory-service",
    "example-key-1",
    key,
)
print(f"attestation={loaded.id}")
print(f"artifact={loaded.artifact_id}")
print(f"payload={loaded.payload_digest}")
print(f"bytes={loaded.payload_bytes}")
print(f"algorithm={rbfsafe.artifact_authentication_algorithm_name(loaded.algorithm)}")
print("verified=true")
