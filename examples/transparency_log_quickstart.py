"""Publish a reviewed deployment and an independent observation to a new log.

The input directory is produced by rbfsafe_bounded_execution_session_quickstart.
All deterministic private keys in this example are synthetic and must never be
used for a real deployment or transparency service.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


TRUST_ROOT_ID = "3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780"
CHECKPOINT_ID = "3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01"
LOG_NAMESPACE = "rbfsafe-public-deployments-v1"


def deterministic_seed(offset: int) -> bytes:
    return bytes((index + offset) & 0xFF for index in range(32))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_directory", type=Path)
    parser.add_argument("new_ledger_directory", type=Path)
    parser.add_argument("new_transparency_directory", type=Path)
    args = parser.parse_args()

    checkpoint = rbfsafe.ServiceTrustCheckpoint.load(
        args.input_directory / "checkpoint.json"
    )
    history = rbfsafe.ServiceTrustHistory.open(
        args.input_directory / "trust-history",
        TRUST_ROOT_ID,
        checkpoint,
        CHECKPOINT_ID,
    )
    reviewed = rbfsafe.ReviewedDeploymentProfile.load(
        args.input_directory / "profile.json",
        history,
        checkpoint,
        CHECKPOINT_ID,
    )
    atlas = rbfsafe.SafeAtlas.load(args.input_directory / "atlas")
    session = rbfsafe.BoundedExecutionSession.load(
        args.input_directory / "session.json",
        reviewed,
        history,
        checkpoint,
        CHECKPOINT_ID,
        atlas,
    )

    anchor = rbfsafe.DeploymentTransparencyAnchor.create(
        reviewed, history, checkpoint, CHECKPOINT_ID
    )
    ledger = rbfsafe.ExecutionLedger.create(args.new_ledger_directory, session)
    command = session.command_sequence.commands[0]
    decision = ledger.authorize_command(
        session,
        reviewed,
        history,
        checkpoint,
        CHECKPOINT_ID,
        atlas,
        command.index,
        command.configuration,
        1_000_000,
        ledger.current_record_id,
    )
    if decision.authorization is None:
        raise RuntimeError("ledger withheld the exact command authorization")

    observation_input = rbfsafe.IndependentRuntimeObservationInput()
    observation_input.runtime = session.monitor_acknowledgement.observation.runtime
    observation_input.observation_sequence = 8
    observation_input.observed_monotonic_ns = 1_000_001
    observation_input.monitor_state = (
        rbfsafe.ExecutionMonitorState.ARMED_CERTIFIED_SEQUENCE
    )
    observation_input.configuration_digest = "4" * 64
    observation = rbfsafe.IndependentRuntimeObservation.create(
        session, ledger, decision.authorization, observation_input
    )

    approvals = {approval.role: approval for approval in reviewed.approval_set.approvals}
    safety_approval = approvals[rbfsafe.DeploymentReviewRole.SAFETY]
    controls_approval = approvals[rbfsafe.DeploymentReviewRole.CONTROLS]
    safety_pair = rbfsafe.ed25519_key_pair_from_seed(deterministic_seed(1))
    controls_pair = rbfsafe.ed25519_key_pair_from_seed(deterministic_seed(33))
    safety_attestation = rbfsafe.sign_runtime_observation(
        observation,
        safety_approval.signer_service_id,
        safety_approval.signer_key_id,
        safety_pair.secret_key,
    )
    controls_attestation = rbfsafe.sign_runtime_observation(
        observation,
        controls_approval.signer_service_id,
        controls_approval.signer_key_id,
        controls_pair.secret_key,
    )
    policy = rbfsafe.RuntimeObservationPolicy()
    policy.minimum_attestations = 2
    attestation_set = rbfsafe.assemble_runtime_observation_attestations(
        session,
        observation,
        policy,
        [controls_attestation, safety_attestation],
        history.current_bundle(),
    )

    transparency_pair = rbfsafe.ed25519_key_pair_from_seed(deterministic_seed(225))
    transparency_key = rbfsafe.make_service_public_key(
        "transparency-log",
        transparency_pair.public_key,
        state=rbfsafe.ServiceKeyState.ACTIVE,
        allow_fetch=False,
        allow_publish=True,
    )
    identity = rbfsafe.TransparencyLogIdentity.create(
        LOG_NAMESPACE,
        transparency_key.service_id,
        transparency_key.id,
        transparency_pair.public_key,
    )
    log = rbfsafe.TransparencyLog.create(args.new_transparency_directory, identity)
    first = log.publish_deployment_anchor(
        anchor, transparency_pair.secret_key, log.current_checkpoint_id
    )
    second = log.publish_runtime_observation(
        attestation_set, transparency_pair.secret_key, log.current_checkpoint_id
    )
    proof = log.inclusion_proof(0)
    rbfsafe.verify_transparency_inclusion(identity, second.checkpoint, first.leaf, proof)
    witness = log.consistency_witness(1)
    rbfsafe.verify_transparency_consistency(
        identity, first.checkpoint, second.checkpoint, witness
    )
    audit = log.audit()

    print(f"log={identity.id}")
    print(f"namespace={identity.log_namespace}")
    print(f"signer_service={identity.signer_service_id}")
    print(f"signer_key={identity.signer_key_id}")
    print(f"signer_public_key={identity.signer_public_key.hex()}")
    print(f"checkpoint={log.current_checkpoint_id}")
    print(f"root={log.current_root_hash}")
    print(f"records={audit.verified_records}")
    print(f"deployment_anchors={audit.deployment_anchor_count}")
    print(f"runtime_observations={audit.runtime_observation_count}")
    print("evidence=unknown")
    print("runtime_executable=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
