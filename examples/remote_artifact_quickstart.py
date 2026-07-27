"""Verify a transport-neutral remote publication and persist its audit record."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

import rbfsafe


parser = argparse.ArgumentParser()
parser.add_argument("journal", type=Path, help="new transfer-journal directory")
args = parser.parse_args()

payload = b"immutable atlas payload\n"
item = rbfsafe.MemoryArtifactInput()
item.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
item.deployment_id = "arm-a"
item.robot_digest = "a" * 64
item.scene_digest = "b" * 64
item.task_id = "shelf-pick"
item.content_digest = hashlib.sha256(payload).hexdigest()
item.locator = "artifacts/shelf-atlas"
item.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
memory = rbfsafe.SafetyMemory()
artifact = memory.register_artifact(item)

# Demonstration key only. Production keys must come from an external secret manager.
key = bytes(range(1, 33))
request = rbfsafe.prepare_artifact_publish(
    memory,
    artifact.id,
    payload,
    "artifact-service",
    1,
    "application/vnd.rbfsafe.atlas",
)
receipt = rbfsafe.make_artifact_publish_receipt(request, 101)
receipt = rbfsafe.authenticate_artifact_publish_receipt(
    receipt, "service-key-1", key
)
verified = rbfsafe.verify_artifact_publish(
    memory, request, receipt, payload, "service-key-1", key
)
journal = rbfsafe.ArtifactTransferJournal()
record = journal.append(verified, "")
journal.save(args.journal)
loaded = rbfsafe.ArtifactTransferJournal.load(args.journal)

print(f"request={request.id}")
print(f"receipt={receipt.id}")
print(f"transfer={verified.id}")
print(f"record={record.id}")
print(f"journal={loaded.identity}")
print(
    "authentication="
    f"{rbfsafe.artifact_transfer_authentication_name(verified.authentication)}"
)
print("runtime_executable=false")
