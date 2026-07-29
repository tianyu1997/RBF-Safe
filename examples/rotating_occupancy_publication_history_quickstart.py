"""Create, rotate, publish, and replay an authenticated occupancy history."""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

import rbfsafe


parser = argparse.ArgumentParser()
parser.add_argument("occupancy_payload", type=Path)
parser.add_argument("history_directory", type=Path)
args = parser.parse_args()

if args.history_directory.exists():
    parser.error(f"history directory already exists: {args.history_directory}")

# Reproducible demonstration seeds only. Production secret keys must remain in
# a protected key manager and never enter a history artifact.
root_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(41, 73)))
successor_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(81, 113)))
root_key = rbfsafe.make_service_public_key(
    "rotating-occupancy-publisher",
    root_pair.public_key,
    1,
    0,
    rbfsafe.ServiceKeyState.ACTIVE,
    False,
    True,
    True,
)
successor_key = rbfsafe.make_service_public_key(
    "rotating-occupancy-publisher",
    successor_pair.public_key,
    2,
    0,
    rbfsafe.ServiceKeyState.ACTIVE,
    False,
    True,
    True,
)
root_bundle = rbfsafe.ServiceTrustBundle.create(1, "", [root_key])

with tempfile.TemporaryDirectory(
    prefix="rbfsafe-rotating-trust-source-"
) as source_parent:
    source_directory = Path(source_parent) / "history"
    source_trust = rbfsafe.ServiceTrustHistory.create(
        source_directory, root_bundle, root_bundle.id
    )
    root_publication = rbfsafe.sign_continuous_fleet_occupancy_publication(
        args.occupancy_payload,
        root_bundle,
        "rotating-cell-stream-v1",
        "rotating-occupancy-publisher",
        root_key.id,
        root_pair.secret_key,
        1,
        "",
        0,
        32,
    )
    history = rbfsafe.RotatingOccupancyPublicationHistory.create(
        args.history_directory,
        root_publication,
        args.occupancy_payload,
        source_trust,
        "rotating-cell-stream-v1",
        "rotating-occupancy-publisher",
        root_bundle.id,
        root_bundle.id,
        root_publication.id,
    )

retired_root_key = rbfsafe.make_service_public_key(
    "rotating-occupancy-publisher",
    root_pair.public_key,
    1,
    1,
    rbfsafe.ServiceKeyState.RETIRED,
    False,
    True,
    True,
)
successor_bundle = rbfsafe.rotate_service_trust_bundle(
    root_bundle, [retired_root_key, successor_key]
)
authorization = rbfsafe.authorize_service_trust_bundle_successor(
    root_bundle,
    successor_bundle,
    root_key.service_id,
    root_key.id,
    root_pair.secret_key,
)
trust_record = history.rotate_trust(
    successor_bundle, authorization, root_bundle.id
)

successor_publication = rbfsafe.sign_continuous_fleet_occupancy_publication(
    args.occupancy_payload,
    successor_bundle,
    "rotating-cell-stream-v1",
    "rotating-occupancy-publisher",
    successor_key.id,
    successor_pair.secret_key,
    2,
    root_publication.id,
    1,
    31,
)
publication_record = history.publish(
    successor_publication,
    args.occupancy_payload,
    root_publication.id,
    successor_bundle.id,
)
reopened = rbfsafe.RotatingOccupancyPublicationHistory.open(
    args.history_directory,
    "rotating-cell-stream-v1",
    "rotating-occupancy-publisher",
    root_bundle.id,
    successor_bundle.id,
    root_publication.id,
    successor_publication.id,
)
reopened.verify(root_publication.id, 16)
reopened.verify(successor_publication.id, 31)
audit = rbfsafe.audit_rotating_occupancy_publication_histories(
    history, reopened
)

print(f"trust_root={root_bundle.id}")
print(f"trust_head={successor_bundle.id}")
print(f"trust_record={trust_record.id}")
print(f"publication_root={root_publication.id}")
print(f"publication_head={successor_publication.id}")
print(f"publication_record={publication_record.id}")
print(f"trust_bundles={len(reopened.trust_history().records)}")
print(f"publications={len(reopened.records)}")
print(
    "trust_relation="
    + rbfsafe.occupancy_publication_history_relation_name(
        audit.trust_relation
    )
)
print(
    "publication_relation="
    + rbfsafe.occupancy_publication_history_relation_name(
        audit.publication_relation
    )
)
print("historical_root_verified=true")
print("current_head_verified=true")
print("evidence=unknown")
print("authorizes_execution=false")
