"""Create and replay a unanimous coordinated occupancy reservation."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


parser = argparse.ArgumentParser()
parser.add_argument("occupancy_payload", type=Path)
parser.add_argument("output_directory", type=Path)
args = parser.parse_args()

if args.output_directory.exists():
    parser.error(f"output directory already exists: {args.output_directory}")
args.output_directory.mkdir(parents=True)

occupancy = rbfsafe.ContinuousFleetOccupancyBundle.load(args.occupancy_payload)
deployment_ids = ["arm-a", "arm-b"]
histories: list[rbfsafe.RotatingOccupancyPublicationHistory] = []

for deployment_id, seed_start in zip(deployment_ids, (11, 51), strict=True):
    pair = rbfsafe.ed25519_key_pair_from_seed(
        bytes(range(seed_start, seed_start + 32))
    )
    service_id = f"{deployment_id}-reservation-publisher"
    stream_id = f"{deployment_id}-reservation-stream-v1"
    key = rbfsafe.make_service_public_key(
        service_id,
        pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        True,
    )
    trust = rbfsafe.ServiceTrustBundle.create(1, "", [key])
    source = rbfsafe.ServiceTrustHistory.create(
        args.output_directory / f"{deployment_id}-source-trust",
        trust,
        trust.id,
    )
    publication = rbfsafe.sign_continuous_fleet_occupancy_publication(
        args.occupancy_payload,
        trust,
        stream_id,
        service_id,
        key.id,
        pair.secret_key,
        1,
        "",
        0,
        32,
    )
    histories.append(
        rbfsafe.RotatingOccupancyPublicationHistory.create(
            args.output_directory / f"{deployment_id}-history",
            publication,
            args.occupancy_payload,
            source,
            stream_id,
            service_id,
            trust.id,
            trust.id,
            publication.id,
        )
    )

agreement = rbfsafe.make_coordinated_reservation_agreement(
    "coordinated-cell-reservation-v1",
    1,
    "",
    16,
    occupancy,
    deployment_ids,
    histories,
)
rbfsafe.verify_coordinated_reservation_agreement(
    agreement, occupancy, deployment_ids, histories
)
agreement.save(args.output_directory / "agreement.json")

print(f"agreement={agreement.id}")
print(f"protocol={agreement.protocol_id}")
print(f"round={agreement.round}")
print(f"occupancy_bundle={agreement.occupancy_bundle_id}")
print(f"participants={len(agreement.participants)}")
print(f"valid_from={agreement.valid_from_tick}")
print(f"valid_through={agreement.valid_through_tick}")
print("unanimous_payload=true")
print("replay_verified=true")
print("evidence=unknown")
print("authorizes_execution=false")
