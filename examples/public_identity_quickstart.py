"""Verify an Ed25519 service response offline and persist public provenance."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

import rbfsafe


parser = argparse.ArgumentParser()
parser.add_argument("bundle", type=Path, help="new public trust-bundle file")
parser.add_argument("journal", type=Path, help="new transfer-journal directory")
parser.add_argument("history", type=Path, help="new service trust-history directory")
parser.add_argument("checkpoint", type=Path, help="new signed trust-checkpoint file")
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

# Reproducible demonstration seed only. Production signing keys must be
# generated with a cryptographically secure RNG and kept in an external HSM
# or secret manager.
key_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
service_key = rbfsafe.make_service_public_key(
    "artifact-service",
    key_pair.public_key,
    1,
    0,
    rbfsafe.ServiceKeyState.ACTIVE,
    True,
    True,
    True,
)
governance_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
governance_key = rbfsafe.make_service_public_key(
    "rotation-governance",
    governance_pair.public_key,
    1,
    0,
    rbfsafe.ServiceKeyState.ACTIVE,
    False,
    False,
    True,
)
rotation_policy = rbfsafe.ServiceTrustRotationPolicy()
rotation_policy.minimum_signatures = 2
rotation_policy.require_distinct_services = True
bundle = rbfsafe.ServiceTrustBundle.create_with_rotation_policy(
    1, "", [service_key, governance_key], rotation_policy
)

request = rbfsafe.prepare_artifact_publish(
    memory,
    artifact.id,
    payload,
    "artifact-service",
    31,
    "application/vnd.rbfsafe.atlas",
    rbfsafe.ArtifactTransferAuthentication.ED25519,
)
receipt = rbfsafe.make_artifact_publish_receipt(request, 101)
receipt = rbfsafe.sign_artifact_publish_receipt(
    receipt, service_key.id, key_pair.secret_key
)
verified = rbfsafe.verify_artifact_publish_offline(
    memory, request, receipt, payload, bundle
)

journal = rbfsafe.ArtifactTransferJournal()
record = journal.append(verified, "")
bundle.save(args.bundle)
journal.save(args.journal)

successor_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(65, 97)))
retired_key = rbfsafe.make_service_public_key(
    "artifact-service",
    key_pair.public_key,
    1,
    1,
    rbfsafe.ServiceKeyState.RETIRED,
    True,
    True,
    True,
)
successor_key = rbfsafe.make_service_public_key(
    "artifact-service",
    successor_pair.public_key,
    2,
    0,
    rbfsafe.ServiceKeyState.ACTIVE,
    True,
    True,
    True,
)
successor = rbfsafe.rotate_service_trust_bundle(
    bundle, [retired_key, successor_key, governance_key]
)
service_authorization = rbfsafe.authorize_service_trust_bundle_successor(
    bundle,
    successor,
    service_key.service_id,
    service_key.id,
    key_pair.secret_key,
)
governance_authorization = rbfsafe.authorize_service_trust_bundle_successor(
    bundle,
    successor,
    governance_key.service_id,
    governance_key.id,
    governance_pair.secret_key,
)
authorizations = rbfsafe.assemble_service_trust_bundle_authorizations(
    bundle, successor, [governance_authorization, service_authorization]
)
history = rbfsafe.ServiceTrustHistory.create(args.history, bundle, bundle.id)
rotation = history.publish(successor, authorizations, bundle.id)
service_checkpoint_signature = rbfsafe.sign_service_trust_checkpoint(
    history,
    successor_key.service_id,
    successor_key.id,
    successor_pair.secret_key,
)
governance_checkpoint_signature = rbfsafe.sign_service_trust_checkpoint(
    history,
    governance_key.service_id,
    governance_key.id,
    governance_pair.secret_key,
)
checkpoint = rbfsafe.assemble_service_trust_checkpoint(
    history, [governance_checkpoint_signature, service_checkpoint_signature]
)
checkpoint.save(args.checkpoint)

print(f"key={service_key.id}")
print(f"bundle={bundle.id}")
print(f"request={request.id}")
print(f"receipt={receipt.id}")
print(f"transfer={verified.id}")
print(f"record={record.id}")
print(f"authorization_set={authorizations.id}")
print(f"rotation={rotation.id}")
print(f"trust_head={history.current_bundle_id}")
print(f"checkpoint={checkpoint.id}")
print("rotation_quorum=2")
print("authentication=ed25519")
print("runtime_executable=false")
