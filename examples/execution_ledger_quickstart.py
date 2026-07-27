"""Replay a bounded session through a new append-only execution ledger.

The input directory is produced by rbfsafe_bounded_execution_session_quickstart.
The synthetic deterministic key is only for this example and must never be used
for a real controller.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


TRUST_ROOT_ID = "3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780"
CHECKPOINT_ID = "3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_directory", type=Path)
    parser.add_argument("new_ledger_directory", type=Path)
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

    controller_seed = bytes(range(97, 129))
    controller_key = rbfsafe.ed25519_key_pair_from_seed(controller_seed)
    ledger = rbfsafe.ExecutionLedger.create(args.new_ledger_directory, session)
    dispatch_times = (1_000_000, 1_050_001, 1_100_000)
    completion_times = (1_000_005, 1_050_005, 1_100_000)

    for command, dispatch_time, completion_time in zip(
        session.command_sequence.commands,
        dispatch_times,
        completion_times,
        strict=True,
    ):
        decision = ledger.authorize_command(
            session,
            reviewed,
            history,
            checkpoint,
            CHECKPOINT_ID,
            atlas,
            command.index,
            command.configuration,
            dispatch_time,
            ledger.current_record_id,
        )
        if decision.authorization is None:
            raise RuntimeError("ledger withheld the exact command authorization")
        completion_input = rbfsafe.ExecutionControllerCompletionInput()
        completion_input.outcome = rbfsafe.ExecutionCompletionOutcome.COMPLETED
        completion_input.completed_monotonic_ns = completion_time
        completion_input.result_digest = str(command.index + 1) * 64
        completion = rbfsafe.sign_execution_controller_completion(
            session,
            decision.authorization,
            completion_input,
            controller_key.secret_key,
        )
        ledger.record_completion(
            session,
            reviewed,
            history,
            atlas,
            completion,
            ledger.current_record_id,
        )

    audit = ledger.audit(session, reviewed, history, atlas)
    print(f"ledger={ledger.id}")
    print(f"head={ledger.current_record_id}")
    print(f"status={rbfsafe.execution_ledger_status_name(audit.status)}")
    print(f"records={audit.verified_records}")
    print("runtime_executable=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
