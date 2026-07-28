"""Sign and verify an exact continuous-fleet occupancy payload offline."""

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
args.output_directory.mkdir()

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
trust_bundle.save(args.output_directory / "trust-bundle.json")

publication = rbfsafe.sign_continuous_fleet_occupancy_publication(
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
publication.save(args.output_directory / "publication.json")
verified = rbfsafe.verify_continuous_fleet_occupancy_publication(
    args.occupancy_payload,
    publication,
    trust_bundle,
    "fixture-cell-occupancy-stream-v1",
    "fixture-occupancy-publisher",
    trust_bundle.id,
    "",
    16,
)

print(f"publication={publication.id}")
print(f"trust_bundle={trust_bundle.id}")
print(f"occupancy_bundle={publication.occupancy_bundle_id}")
print(f"evaluation_tick={verified.evaluation_tick}")
print("authentication=ed25519")
print("evidence=unknown")
print("authorizes_execution=false")
