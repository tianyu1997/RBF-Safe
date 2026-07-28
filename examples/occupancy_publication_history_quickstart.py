"""Create and replay a pinned authenticated occupancy publication history."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


parser = argparse.ArgumentParser()
parser.add_argument("occupancy_payload", type=Path)
parser.add_argument("history_directory", type=Path)
args = parser.parse_args()

if args.history_directory.exists():
    parser.error(f"history directory already exists: {args.history_directory}")

# Reproducible demonstration seed only. Production keys must come from a
# cryptographically secure key manager and never be embedded in artifacts.
key_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
service_key = rbfsafe.make_service_public_key(
    "fixture-occupancy-publisher",
    key_pair.public_key,
    1,
    0,
    rbfsafe.ServiceKeyState.ACTIVE,
    False,
    True,
    False,
)
trust_bundle = rbfsafe.ServiceTrustBundle.create(1, "", [service_key])
root = rbfsafe.sign_continuous_fleet_occupancy_publication(
    args.occupancy_payload,
    trust_bundle,
    "fixture-cell-occupancy-stream-v1",
    "fixture-occupancy-publisher",
    service_key.id,
    key_pair.secret_key,
    1,
    "",
    0,
    32,
)
history = rbfsafe.OccupancyPublicationHistory.create(
    args.history_directory,
    root,
    args.occupancy_payload,
    trust_bundle,
    "fixture-cell-occupancy-stream-v1",
    "fixture-occupancy-publisher",
    trust_bundle.id,
    root.id,
)
successor = rbfsafe.sign_continuous_fleet_occupancy_publication(
    args.occupancy_payload,
    trust_bundle,
    "fixture-cell-occupancy-stream-v1",
    "fixture-occupancy-publisher",
    service_key.id,
    key_pair.secret_key,
    2,
    root.id,
    1,
    31,
)
record = history.publish(successor, args.occupancy_payload, root.id)
reopened = rbfsafe.OccupancyPublicationHistory.open(
    args.history_directory,
    "fixture-cell-occupancy-stream-v1",
    "fixture-occupancy-publisher",
    trust_bundle.id,
    root.id,
    successor.id,
)
audit = rbfsafe.audit_occupancy_publication_histories(history, reopened)

print(f"root={root.id}")
print(f"head={successor.id}")
print(f"head_record={record.id}")
print(f"trust_bundle={trust_bundle.id}")
print(f"publications={len(reopened.records)}")
print(
    "self_relation="
    + rbfsafe.occupancy_publication_history_relation_name(audit.relation)
)
print("replay_verified=true")
print("evidence=unknown")
print("authorizes_execution=false")
