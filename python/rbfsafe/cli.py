"""Inspect and optionally visualize RBF-Safe certificate databases."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from . import (
    ArtifactTransferJournal,
    AtlasUpdater,
    AtlasVersionStore,
    BoundedExecutionSession,
    ContinuousFleetOccupancyBundle,
    ContinuousFleetOccupancyOptions,
    ExecutionLedger,
    FleetScheduleArchive,
    HipacCorridor,
    MemoryArtifactState,
    MemoryArtifactType,
    PolicyFeedbackDatabase,
    PolicyFeedbackLabel,
    PolicyFeedbackQuery,
    PolicyCalibrationLifecycle,
    PolicyCalibrationProfile,
    Pose3d,
    RegionDatabase,
    RegionQueryOptions,
    RegionType,
    ReviewedDeploymentProfile,
    SafeAtlas,
    SafeIkSolver,
    SafeIkStatus,
    SafetyMemory,
    SafetyMemoryStore,
    SceneSnapshot,
    SerialRobotModel,
    ServiceTrustBundle,
    ServiceTrustCheckpoint,
    ServiceTrustHistory,
    TrajectoryAuditor,
    TrajectoryAuditOptions,
    TrajectoryAuditStatus,
    TransparencyGossipArchive,
    TransparencyLog,
    TransparencyLogIdentity,
    VerifiableProvenanceBundle,
    artifact_authentication_algorithm_name,
    artifact_transfer_authentication_name,
    artifact_transfer_operation_name,
    analyze_continuous_fleet_occupancy,
    continuous_fleet_occupancy_status_name,
    fleet_schedule_status_name,
    load_artifact_attestation,
    policy_feedback_label_name,
    memory_artifact_state_name,
    memory_artifact_type_name,
    memory_event_type_name,
    policy_calibration_drift_reason_name,
    policy_calibration_drift_status_name,
    policy_calibration_lifecycle_state_name,
    deployment_review_role_name,
    execution_monitor_state_name,
    execution_ledger_status_name,
    external_time_freshness_status_name,
    hardware_provenance_status_name,
    replay_verifiable_provenance,
    service_key_state_name,
    service_trust_rotation_event_type_name,
    transparency_leaf_kind_name,
    transparency_gossip_conflict_type_name,
    transparency_gossip_status_name,
    verify_artifact_file,
    verify_robot_trajectory_occupancy,
)

_MAX_INSPECTABLE_JSON_BYTES = 268_435_456


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="rbfsafe-inspect")
    parser.add_argument(
        "atlas",
        type=Path,
        help="Atlas/database/archive directory or supported JSON artifact",
    )
    parser.add_argument("--plot", type=Path, help="write a 2-D slice image")
    parser.add_argument("--query", nargs="+", type=float, metavar="Q", help="query one configuration")
    parser.add_argument("--include-portals", action="store_true", help="include portal records in a query")
    parser.add_argument(
        "--include-tubes", action="store_true", help="include trajectory-tube records in a query"
    )
    parser.add_argument("--trajectory", type=Path, help="audit a JSON waypoint array")
    parser.add_argument("--robot", type=Path, help="robot JSON used for a Safe IK query")
    parser.add_argument("--scene", type=Path, help="scene JSON used for a Safe IK query")
    parser.add_argument("--previous-scene", type=Path, help="scene JSON currently bound to the Atlas")
    parser.add_argument("--next-scene", type=Path, help="new scene JSON for an incremental update")
    parser.add_argument("--update-output", type=Path, help="save the incrementally updated Atlas here")
    parser.add_argument("--repair-samples", type=Path, help="optional JSON array of local repair samples")
    parser.add_argument("--store-version", help="load a specific version from an Atlas version store")
    parser.add_argument("--publish-atlas", type=Path, help="publish an Atlas into a version store")
    parser.add_argument("--rollback-version", help="move a version store head to an existing version")
    parser.add_argument(
        "--ik-target",
        nargs=7,
        type=float,
        metavar=("X", "Y", "Z", "QX", "QY", "QZ", "QW"),
        help="solve a position/quaternion target with certified connectivity",
    )
    parser.add_argument("--seed", nargs="+", type=float, metavar="Q", help="Safe IK seed state")
    parser.add_argument(
        "--max-region-tests",
        type=int,
        default=10_000_000,
        help="trajectory audit work budget (default: 10000000)",
    )
    parser.add_argument("--dims", nargs=2, type=int, default=(0, 1), metavar=("X", "Y"))
    parser.add_argument("--fixed", nargs="*", type=float, help="fixed configuration for non-plotted dimensions")
    parser.add_argument("--policy-id", help="filter a policy feedback database by policy ID")
    parser.add_argument("--task-id", help="filter a policy feedback database by task ID")
    parser.add_argument("--episode-id", help="filter a policy feedback database by episode ID")
    parser.add_argument(
        "--feedback-label",
        choices=(
            "selected_accepted",
            "selected_repaired",
            "eligible_not_selected",
            "policy_rejected",
            "shield_rejected",
        ),
        help="filter a policy feedback database by training label",
    )
    parser.add_argument(
        "--max-feedback-results",
        type=int,
        default=100_000,
        help="policy feedback query budget (default: 100000)",
    )
    parser.add_argument("--deployment-id", help="filter a safety memory by deployment ID")
    parser.add_argument(
        "--memory-state",
        choices=("active", "stale", "quarantined", "retired"),
        help="filter a safety memory by lifecycle state",
    )
    parser.add_argument(
        "--artifact-type",
        choices=(
            "safe_atlas",
            "region_database",
            "safe_corridor",
            "trajectory_audit",
            "policy_feedback",
            "runtime_trace",
            "fleet_schedule",
        ),
        help="filter a safety memory by artifact type",
    )
    parser.add_argument("--include-memory-events", action="store_true", help="list safety-memory audit events")
    parser.add_argument("--memory-revision", help="load a specific revision from a safety-memory store")
    parser.add_argument(
        "--fleet-schedule-version",
        help="inspect a specific version from a fleet-schedule archive",
    )
    parser.add_argument("--artifact-payload", type=Path, help="payload to verify against an attestation")
    parser.add_argument(
        "--attestation-memory", type=Path, help="safety-memory directory containing the attested artifact"
    )
    parser.add_argument("--hmac-key-file", type=Path, help="external HMAC key used to verify an attestation")
    parser.add_argument("--expected-service-id", help="trusted service ID for attestation verification")
    parser.add_argument("--expected-key-id", help="trusted key ID for attestation verification")
    parser.add_argument(
        "--expected-trust-root",
        help="caller-pinned root bundle ID required for a service trust history",
    )
    parser.add_argument(
        "--expected-trust-head",
        help="caller-retained current bundle ID required for a service trust history",
    )
    parser.add_argument(
        "--trust-checkpoint",
        type=Path,
        help="signed checkpoint used to verify a service trust history",
    )
    parser.add_argument(
        "--trust-history",
        type=Path,
        help="service trust-history directory used to verify a checkpoint file",
    )
    parser.add_argument(
        "--expected-trust-checkpoint",
        help="caller-retained checkpoint ID required for checkpoint verification",
    )
    parser.add_argument(
        "--policy-confidence",
        type=float,
        help="raw confidence to map through a policy-calibration profile",
    )
    parser.add_argument(
        "--calibration-profile",
        type=Path,
        help="profile JSON required to validate a policy-calibration lifecycle",
    )
    parser.add_argument(
        "--reviewed-profile",
        type=Path,
        help="reviewed deployment profile required to verify an execution session",
    )
    parser.add_argument(
        "--execution-atlas",
        type=Path,
        help="SafeAtlas directory required to replay an execution session",
    )
    parser.add_argument(
        "--execution-session",
        type=Path,
        help="bounded execution-session JSON required to audit an execution ledger",
    )
    parser.add_argument(
        "--transparency-namespace",
        help="caller-pinned namespace required to inspect a transparency log",
    )
    parser.add_argument(
        "--transparency-service-id",
        help="caller-pinned transparency signer service ID",
    )
    parser.add_argument(
        "--transparency-key-id",
        help="caller-pinned transparency signer key ID",
    )
    parser.add_argument(
        "--transparency-public-key",
        help="caller-pinned Ed25519 transparency public key as 64 lowercase hex characters",
    )
    parser.add_argument(
        "--expected-transparency-checkpoint",
        help="caller-retained current transparency checkpoint ID",
    )
    parser.add_argument(
        "--expected-gossip-trust-bundle",
        help="caller-retained exact trust-bundle ID for a transparency gossip archive",
    )
    parser.add_argument(
        "--expected-gossip-head",
        help="caller-retained current record ID for a transparency gossip archive",
    )
    parser.add_argument(
        "--evaluated-at-ns",
        type=int,
        help="caller-supplied external-clock evaluation time for provenance freshness",
    )
    parser.add_argument(
        "--execution-command-index",
        type=int,
        help="exact command index to evaluate in a bounded execution session",
    )
    parser.add_argument(
        "--execution-configuration",
        nargs="+",
        type=float,
        metavar="Q",
        help="exact command configuration to evaluate",
    )
    parser.add_argument(
        "--occupancy-robot",
        action="append",
        default=[],
        metavar="DEPLOYMENT=ROBOT_JSON",
        help=(
            "replay one continuous occupancy against the exact robot model; "
            "repeat once for every deployment"
        ),
    )
    parser.add_argument(
        "--occupancy-minimum-separation",
        type=float,
        help="reanalyze a continuous fleet occupancy bundle with this separation margin",
    )
    parser.add_argument(
        "--dispatch-monotonic-ns",
        type=int,
        help="caller-supplied monotonic dispatch time for exact command evaluation",
    )
    parser.add_argument(
        "--max-memory-results",
        type=int,
        default=100_000,
        help="safety-memory inspection budget (default: 100000)",
    )
    return parser


def _print_safety_memory(memory: SafetyMemory, args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    if args.max_memory_results <= 0:
        parser.error("--max-memory-results must be positive")
    states = {
        "active": MemoryArtifactState.ACTIVE,
        "stale": MemoryArtifactState.STALE,
        "quarantined": MemoryArtifactState.QUARANTINED,
        "retired": MemoryArtifactState.RETIRED,
    }
    types = {
        "safe_atlas": MemoryArtifactType.SAFE_ATLAS,
        "region_database": MemoryArtifactType.REGION_DATABASE,
        "safe_corridor": MemoryArtifactType.SAFE_CORRIDOR,
        "trajectory_audit": MemoryArtifactType.TRAJECTORY_AUDIT,
        "policy_feedback": MemoryArtifactType.POLICY_FEEDBACK,
        "runtime_trace": MemoryArtifactType.RUNTIME_TRACE,
        "fleet_schedule": MemoryArtifactType.FLEET_SCHEDULE,
    }
    artifacts = [
        artifact
        for artifact in memory.artifacts
        if (args.deployment_id is None or artifact.deployment_id == args.deployment_id)
        and (args.task_id is None or artifact.task_id == args.task_id)
        and (args.memory_state is None or artifact.state == states[args.memory_state])
        and (args.artifact_type is None or artifact.type == types[args.artifact_type])
    ]
    if len(artifacts) > args.max_memory_results:
        parser.error("safety-memory result count exceeds --max-memory-results")
    summary = memory.summary
    print(
        f"artifacts={summary.artifacts} active={summary.active} stale={summary.stale} "
        f"quarantined={summary.quarantined} retired={summary.retired} "
        f"events={summary.events} recorded_reuses={summary.recorded_reuses}"
    )
    print(f"memory_identity={memory.identity}")
    print(f"query_artifacts={len(artifacts)}")
    for artifact in artifacts:
        print(
            f"artifact={artifact.id} type={memory_artifact_type_name(artifact.type)} "
            f"state={memory_artifact_state_name(artifact.state)} "
            f"deployment={artifact.deployment_id} task={artifact.task_id} "
            f"generation={artifact.generation} locator={artifact.locator}"
        )
    if args.include_memory_events:
        selected_ids = {artifact.id for artifact in artifacts}
        events = [event for event in memory.events if event.artifact_id in selected_ids]
        if len(events) > args.max_memory_results:
            parser.error("safety-memory event count exceeds --max-memory-results")
        print(f"query_events={len(events)}")
        for event in events:
            print(
                f"event={event.id} sequence={event.sequence} "
                f"type={memory_event_type_name(event.type)} artifact={event.artifact_id} "
                f"task={event.task_id} detail={event.detail}"
            )


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    file_document: dict[str, object] = {}
    try:
        if (
            args.atlas.is_file()
            and args.atlas.stat().st_size <= _MAX_INSPECTABLE_JSON_BYTES
        ):
            candidate = json.loads(args.atlas.read_text(encoding="utf-8"))
            if isinstance(candidate, dict):
                file_document = candidate
    except (OSError, UnicodeError, json.JSONDecodeError):
        pass
    if (
        file_document.get("format")
        == "rbfsafe-continuous-fleet-occupancy-bundle"
    ):
        bundle = ContinuousFleetOccupancyBundle.load(args.atlas)
        report = bundle.report
        if args.occupancy_minimum_separation is not None:
            if (
                not math.isfinite(args.occupancy_minimum_separation)
                or args.occupancy_minimum_separation < 0.0
            ):
                parser.error(
                    "--occupancy-minimum-separation must be finite and non-negative"
                )
            options = ContinuousFleetOccupancyOptions()
            options.minimum_separation = args.occupancy_minimum_separation
            report = analyze_continuous_fleet_occupancy(
                bundle.occupancies, options
            )

        verified_deployments: set[str] = set()
        robot_paths: dict[str, Path] = {}
        for specification in args.occupancy_robot:
            if "=" not in specification:
                parser.error(
                    "--occupancy-robot must use DEPLOYMENT=ROBOT_JSON"
                )
            deployment, robot_path = specification.split("=", 1)
            if not deployment or not robot_path or deployment in robot_paths:
                parser.error(
                    "--occupancy-robot deployment and path must be non-empty and unique"
                )
            robot_paths[deployment] = Path(robot_path)
        if robot_paths:
            expected_deployments = {
                occupancy.deployment_id for occupancy in bundle.occupancies
            }
            if set(robot_paths) != expected_deployments:
                parser.error(
                    "--occupancy-robot must provide exactly one model for every deployment"
                )
            for occupancy in bundle.occupancies:
                robot = SerialRobotModel.from_json(
                    robot_paths[occupancy.deployment_id]
                )
                verify_robot_trajectory_occupancy(robot, occupancy)
                verified_deployments.add(occupancy.deployment_id)

        print(
            "RBF-Safe continuous-fleet-occupancy-bundle "
            f"schema={bundle.storage_schema}"
        )
        print(f"bundle_id={bundle.id}")
        print(
            f"timeline={report.timeline_id} "
            f"workspace_frame={report.workspace_frame_id}"
        )
        print(
            "status="
            + continuous_fleet_occupancy_status_name(report.status)
        )
        print(
            f"occupancies={len(bundle.occupancies)} "
            f"slices={sum(len(item.slices) for item in bundle.occupancies)} "
            f"conflicts={len(report.conflicts)}"
        )
        identity_rotation = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
        print(
            "rotated_frames="
            + str(
                sum(
                    tuple(item.workspace_rotation) != identity_rotation
                    for item in bundle.occupancies
                )
            )
            + " uncertain_frames="
            + str(
                sum(
                    item.workspace_angular_uncertainty_radians > 0.0
                    or any(
                        value > 0.0
                        for value in item.workspace_translation_uncertainty
                    )
                    for item in bundle.occupancies
                )
            )
        )
        print(
            f"minimum_separation={report.minimum_separation} "
            f"slice_pair_evaluations={report.slice_pair_evaluations} "
            f"link_pair_evaluations={report.link_pair_evaluations}"
        )
        print(
            "robot_replay_verified="
            + str(len(verified_deployments) == len(bundle.occupancies)).lower()
        )
        print("evidence=unknown")
        print("authorizes_execution=false")
        return 0
    if (
        args.occupancy_robot
        or args.occupancy_minimum_separation is not None
    ):
        parser.error(
            "continuous occupancy options require a continuous fleet occupancy bundle"
        )
    if file_document.get("format") == "rbfsafe-verifiable-provenance-bundle":
        required = (
            args.trust_history,
            args.trust_checkpoint,
            args.expected_trust_root,
            args.expected_trust_checkpoint,
            args.evaluated_at_ns,
        )
        if any(value is None for value in required):
            parser.error(
                "provenance inspection requires --trust-history, "
                "--trust-checkpoint, --expected-trust-root, "
                "--expected-trust-checkpoint, and --evaluated-at-ns"
            )
        if args.evaluated_at_ns < 0:
            parser.error("--evaluated-at-ns must be non-negative")
        provenance = VerifiableProvenanceBundle.load(args.atlas)
        checkpoint = ServiceTrustCheckpoint.load(args.trust_checkpoint)
        history = ServiceTrustHistory.open(
            args.trust_history,
            args.expected_trust_root,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        trust_bundle = history.bundle(provenance.trust_bundle_id)
        audit = replay_verifiable_provenance(
            provenance, trust_bundle, args.evaluated_at_ns
        )
        print(
            "RBF-Safe verifiable-provenance-bundle "
            f"schema={provenance.storage_schema}"
        )
        print(f"bundle_id={provenance.id}")
        print(
            f"subject_service={provenance.subject_key.service_id} "
            f"subject_key={provenance.subject_key.id}"
        )
        print(f"trust_bundle={provenance.trust_bundle_id}")
        print(
            "hardware_status="
            + hardware_provenance_status_name(audit.hardware.status)
        )
        print(
            f"hardware_statements={audit.hardware.authenticated_statement_count} "
            f"distinct_attesters={audit.hardware.distinct_attester_count}"
        )
        print(
            "freshness_status="
            + external_time_freshness_status_name(audit.freshness.status)
        )
        print(
            f"time_sources={audit.freshness.authenticated_source_count} "
            f"evaluated_at_ns={audit.freshness.evaluated_at_ns}"
        )
        print(f"ready={str(audit.ready).lower()}")
        print("evidence=unknown")
        print("authorizes_execution=false")
        return 0
    if file_document.get("format") == "rbfsafe-bounded-execution-session":
        required = (
            args.reviewed_profile,
            args.execution_atlas,
            args.trust_history,
            args.trust_checkpoint,
            args.expected_trust_root,
            args.expected_trust_checkpoint,
        )
        if any(value is None for value in required):
            parser.error(
                "execution-session inspection requires --reviewed-profile, "
                "--execution-atlas, --trust-history, --trust-checkpoint, "
                "--expected-trust-root, and --expected-trust-checkpoint"
            )
        checkpoint = ServiceTrustCheckpoint.load(args.trust_checkpoint)
        history = ServiceTrustHistory.open(
            args.trust_history,
            args.expected_trust_root,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        reviewed = ReviewedDeploymentProfile.load(
            args.reviewed_profile,
            history,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        atlas = SafeAtlas.load(args.execution_atlas)
        session = BoundedExecutionSession.load(
            args.atlas,
            reviewed,
            history,
            checkpoint,
            args.expected_trust_checkpoint,
            atlas,
        )
        print(
            f"RBF-Safe bounded-execution-session schema=1 "
            f"commands={len(session.command_sequence.commands)} "
            f"approvals={len(session.approval_set.approvals)}"
        )
        print(f"session_id={session.id}")
        print(f"request_id={session.request.id}")
        print(f"reviewed_profile={session.request.reviewed_profile_id}")
        print(f"atlas={session.request.atlas_id}")
        print(
            f"controller={session.request.controller.service_id} "
            f"monitor={session.request.runtime_monitor.service_id}"
        )
        print(
            "monitor_state="
            + execution_monitor_state_name(
                session.monitor_acknowledgement.observation.monitor_state
            )
        )
        print(
            f"valid_from_monotonic_ns={session.valid_from_monotonic_ns} "
            f"start_deadline_monotonic_ns={session.start_deadline_monotonic_ns} "
            f"valid_through_monotonic_ns={session.valid_through_monotonic_ns}"
        )
        print("session_evidence=unknown")
        print(
            "session_authorizes_execution="
            + str(session.authorizes_execution).lower()
        )
        exact_arguments = (
            args.execution_command_index,
            args.execution_configuration,
            args.dispatch_monotonic_ns,
        )
        if any(value is not None for value in exact_arguments):
            if not all(value is not None for value in exact_arguments):
                parser.error(
                    "--execution-command-index, --execution-configuration, "
                    "and --dispatch-monotonic-ns must be used together"
                )
            if (
                args.execution_command_index < 0
                or args.dispatch_monotonic_ns <= 0
                or len(args.execution_configuration)
                != session.command_sequence.dimension
                or not all(
                    math.isfinite(value)
                    for value in args.execution_configuration
                )
            ):
                parser.error("exact execution command input is invalid")
            authorization = session.authorize_command(
                args.execution_command_index,
                args.execution_configuration,
                args.dispatch_monotonic_ns,
            )
            print(
                "command_authorized="
                + str(authorization is not None).lower()
            )
            if authorization is not None:
                print(f"command_authorization={authorization.id}")
                print("command_evidence=runtime_executable")
                print(
                    "command_open_ended="
                    + str(authorization.open_ended).lower()
                )
        else:
            print(
                "command_authorization_requires_exact_runtime_input=true"
            )
        return 0
    attestation_arguments = (
        args.artifact_payload,
        args.attestation_memory,
        args.hmac_key_file,
        args.expected_service_id,
        args.expected_key_id,
    )
    trust_arguments = (
        args.expected_trust_root,
        args.expected_trust_head,
        args.trust_checkpoint,
        args.trust_history,
        args.expected_trust_checkpoint,
    )
    if file_document.get("format") == "rbfsafe-reviewed-deployment-profile":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.policy_confidence,
            args.calibration_profile,
            args.expected_trust_head,
            args.artifact_payload,
            args.attestation_memory,
            args.hmac_key_file,
            args.expected_service_id,
            args.expected_key_id,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error(
                "Atlas, memory, policy, attestation, and query options do not "
                "apply to reviewed deployment profiles"
            )
        if (
            args.expected_trust_root is None
            or args.trust_history is None
            or args.trust_checkpoint is None
            or args.expected_trust_checkpoint is None
        ):
            parser.error(
                "--expected-trust-root, --trust-history, --trust-checkpoint, and "
                "--expected-trust-checkpoint are required for a reviewed "
                "deployment profile"
            )
        checkpoint = ServiceTrustCheckpoint.load(args.trust_checkpoint)
        history = ServiceTrustHistory.open(
            args.trust_history,
            args.expected_trust_root,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        reviewed = ReviewedDeploymentProfile.load(
            args.atlas,
            history,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        profile = reviewed.profile
        approval_set = reviewed.approval_set
        print(
            f"RBF-Safe reviewed-deployment-profile "
            f"schema={profile.storage_schema}"
        )
        print(
            f"profile={profile.id} deployment={profile.deployment_id} "
            f"robot={profile.robot_digest} controller={profile.controller_digest} "
            f"platform={profile.platform_digest} runtime={profile.runtime_digest}"
        )
        print(
            f"trust_root={profile.trust_root_bundle_id} "
            f"trust_checkpoint={profile.trust_checkpoint_id} "
            f"trust_bundle={profile.trust_bundle_id} "
            f"trust_sequence={profile.trust_bundle_sequence}"
        )
        print(
            f"approval_set={approval_set.id} approvals={len(approval_set.approvals)} "
            f"minimum_approvals={profile.review_policy.minimum_approvals} "
            f"distinct_services="
            f"{str(profile.review_policy.require_distinct_services).lower()}"
        )
        for role in profile.review_policy.required_roles:
            print(f"required_role={deployment_review_role_name(role)}")
        for approval in approval_set.approvals:
            print(
                f"approval={approval.id} signer_service={approval.signer_service_id} "
                f"signer_key={approval.signer_key_id} "
                f"role={deployment_review_role_name(approval.role)}"
            )
        print("caller_pinned=true")
        print("checkpoint_verified=true")
        print("review_signatures_verified=true")
        print("runtime_executable=false")
        return 0
    if file_document.get("format") == "rbfsafe-service-trust-checkpoint":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.policy_confidence,
            args.calibration_profile,
            args.expected_trust_head,
            args.trust_checkpoint,
            *attestation_arguments,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error(
                "Atlas, memory, policy, attestation, and query options do not "
                "apply to service trust checkpoints"
            )
        if (
            args.expected_trust_root is None
            or args.trust_history is None
            or args.expected_trust_checkpoint is None
        ):
            parser.error(
                "--expected-trust-root, --trust-history, and "
                "--expected-trust-checkpoint are required for a trust checkpoint"
            )
        checkpoint = ServiceTrustCheckpoint.load(args.atlas)
        history = ServiceTrustHistory.open(
            args.trust_history,
            args.expected_trust_root,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        print(
            f"RBF-Safe service-trust-checkpoint schema={checkpoint.storage_schema}"
        )
        print(
            f"checkpoint={checkpoint.id} root={checkpoint.root_bundle_id} "
            f"head={checkpoint.head_bundle_id} sequence={checkpoint.head_sequence} "
            f"record={checkpoint.head_record_id} signatures={len(checkpoint.signatures)}"
        )
        for signature in checkpoint.signatures:
            print(
                f"signer_service={signature.signer_service_id} "
                f"signer_key={signature.signer_key_id} "
                f"algorithm={artifact_authentication_algorithm_name(signature.algorithm)}"
            )
        print(f"history_schema={history.storage_schema}")
        print("caller_pinned=true")
        print("checkpoint_verified=true")
        print("runtime_executable=false")
        return 0
    if file_document.get("format") == "rbfsafe-service-trust-bundle":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.policy_confidence,
            args.calibration_profile,
            *attestation_arguments,
            *trust_arguments,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error(
                "Atlas, memory, policy, attestation, and query options do not "
                "apply to service trust bundles"
            )
        bundle = ServiceTrustBundle.load(args.atlas)
        print(f"RBF-Safe service-trust-bundle schema={bundle.storage_schema}")
        print(
            f"bundle={bundle.id} sequence={bundle.sequence} "
            f"parent={bundle.parent_id or '-'} keys={len(bundle.keys)}"
        )
        print(
            f"rotation_minimum_signatures={bundle.rotation_policy.minimum_signatures} "
            f"rotation_distinct_services="
            f"{str(bundle.rotation_policy.require_distinct_services).lower()}"
        )
        for key in bundle.keys:
            upper = key.valid_through_sequence or "-"
            print(
                f"service={key.service_id} key={key.id} "
                f"algorithm={artifact_authentication_algorithm_name(key.algorithm)} "
                f"state={service_key_state_name(key.state)} "
                f"sequences={key.valid_from_sequence}:{upper} "
                f"fetch={str(key.allow_fetch).lower()} "
                f"publish={str(key.allow_publish).lower()} "
                f"rotate={str(key.allow_rotate).lower()}"
            )
        print("caller_pinned=false")
        print("runtime_executable=false")
        return 0
    if (
        file_document.get("format") == "rbfsafe-policy-calibration-lifecycle"
        or args.calibration_profile is not None
    ):
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.policy_confidence,
            *attestation_arguments,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error("Atlas, memory, fleet, attestation, and query options do not apply to calibration lifecycles")
        if args.calibration_profile is None:
            parser.error("--calibration-profile is required for a policy-calibration lifecycle")
        profile = PolicyCalibrationProfile.load(args.calibration_profile)
        lifecycle = PolicyCalibrationLifecycle.load(args.atlas, profile)
        print("RBF-Safe policy-calibration-lifecycle schema=1")
        print(
            f"profile={lifecycle.profile_id} state={policy_calibration_lifecycle_state_name(lifecycle.state)} "
            f"generation={lifecycle.generation}"
        )
        print(
            f"head={lifecycle.current_event_id} latest_report={lifecycle.latest_report_id or '-'}"
        )
        print(
            f"assessments={lifecycle.summary.assessments} stable={lifecycle.summary.stable} "
            f"insufficient_data={lifecycle.summary.insufficient_data} "
            f"drift_detected={lifecycle.summary.drift_detected} "
            f"transitions={lifecycle.summary.transitions}"
        )
        if lifecycle.reports:
            report = lifecycle.latest_report()
            reasons = ",".join(policy_calibration_drift_reason_name(value) for value in report.reasons)
            print(
                f"window={report.window_id} window_sequence={report.window_sequence} "
                f"status={policy_calibration_drift_status_name(report.status)} "
                f"samples={report.sample_count}"
            )
            print(
                f"total_variation_distance={report.total_variation_distance} "
                f"expected_calibration_error={report.expected_calibration_error} "
                f"overall_success_rate_drop={report.overall_success_rate_drop} "
                f"maximum_bin_success_rate_drop={report.maximum_bin_success_rate_drop} "
                f"reasons={reasons or '-'}"
            )
        print(f"deployment_ready={str(lifecycle.deployment_ready).lower()}")
        print("runtime_executable=false")
        return 0
    if file_document.get("format") == "rbfsafe-policy-calibration-profile":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.calibration_profile,
            *attestation_arguments,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error("Atlas, memory, fleet, and attestation options do not apply to calibration profiles")
        profile = PolicyCalibrationProfile.load(args.atlas)
        print("RBF-Safe policy-calibration-profile schema=1")
        print(
            f"profile={profile.id} policy={profile.policy_id} model={profile.policy_model_digest}"
        )
        print(f"scope={profile.scope_id} task={profile.task_id} dataset={profile.dataset_digest}")
        print(
            f"method={profile.method} method_version={profile.method_version} "
            f"samples={profile.sample_count} bins={len(profile.bins)}"
        )
        print(
            f"expected_calibration_error={profile.expected_calibration_error} "
            f"maximum_calibration_error={profile.maximum_calibration_error}"
        )
        print("runtime_executable=false")
        if args.policy_confidence is not None:
            lookup = profile.lookup(args.policy_confidence)
            print(
                f"lookup_bin={lookup.bin_index} raw_confidence={lookup.raw_confidence} "
                f"calibrated_confidence={lookup.calibrated_confidence} "
                f"conservative_confidence={lookup.conservative_confidence} "
                f"bin_samples={lookup.samples}"
            )
        return 0
    if file_document.get("format") == "rbfsafe-artifact-attestation":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.policy_confidence,
            args.calibration_profile,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error("Atlas, memory-query, fleet, and planning options do not apply to attestations")
        attestation = load_artifact_attestation(args.atlas)
        print("RBF-Safe artifact-attestation schema=1")
        print(
            f"attestation={attestation.id} service={attestation.service_id} key={attestation.key_id} "
            f"algorithm={artifact_authentication_algorithm_name(attestation.algorithm)}"
        )
        print(
            f"artifact={attestation.artifact_id} generation={attestation.artifact_generation} "
            f"state={memory_artifact_state_name(attestation.artifact_state)}"
        )
        print(
            f"payload={attestation.payload_digest} bytes={attestation.payload_bytes} "
            f"media_type={attestation.media_type}"
        )
        if not any(value is not None for value in attestation_arguments):
            print("verified=false")
            return 0
        if not all(value is not None for value in attestation_arguments):
            parser.error(
                "--artifact-payload, --attestation-memory, --hmac-key-file, "
                "--expected-service-id, and --expected-key-id must be used together"
            )
        try:
            key = args.hmac_key_file.read_bytes()
        except OSError as error:
            parser.error(f"cannot read --hmac-key-file: {error}")
        if not 32 <= len(key) <= 4096:
            parser.error("--hmac-key-file must contain 32 to 4096 bytes")
        memory = SafetyMemory.load(args.attestation_memory)
        artifact = memory.artifact(attestation.artifact_id)
        if artifact is None:
            parser.error("attested artifact is not present in --attestation-memory")
        verify_artifact_file(
            artifact,
            args.artifact_payload,
            attestation,
            args.expected_service_id,
            args.expected_key_id,
            key,
        )
        print("verified=true")
        return 0
    if any(value is not None for value in attestation_arguments):
        parser.error("artifact-attestation verification options require an attestation JSON file")
    if args.policy_confidence is not None:
        parser.error("--policy-confidence requires a policy-calibration profile JSON file")
    try:
        manifest = json.loads((args.atlas / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        manifest = {}
    try:
        store_manifest = json.loads((args.atlas / "store.json").read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        store_manifest = {}
    feedback_filters = (args.policy_id, args.task_id, args.episode_id, args.feedback_label)
    memory_filters = (args.deployment_id, args.memory_state, args.artifact_type, args.memory_revision)
    if (
        args.execution_session is not None
        and manifest.get("format") != "rbfsafe-execution-ledger"
    ):
        parser.error("--execution-session applies only to an execution-ledger directory")
    transparency_arguments = (
        args.transparency_namespace,
        args.transparency_service_id,
        args.transparency_key_id,
        args.transparency_public_key,
    )
    transparency_formats = {
        "rbfsafe-transparency-log",
        "rbfsafe-transparency-gossip-archive",
    }
    if (
        any(value is not None for value in transparency_arguments)
        and manifest.get("format") not in transparency_formats
    ):
        parser.error(
            "transparency identity options apply only to transparency log or gossip directories"
        )
    if (
        args.expected_transparency_checkpoint is not None
        and manifest.get("format") != "rbfsafe-transparency-log"
    ):
        parser.error(
            "--expected-transparency-checkpoint applies only to a transparency log"
        )
    gossip_arguments = (
        args.expected_gossip_trust_bundle,
        args.expected_gossip_head,
    )
    if (
        any(value is not None for value in gossip_arguments)
        and manifest.get("format") != "rbfsafe-transparency-gossip-archive"
    ):
        parser.error(
            "gossip identity options apply only to a transparency-gossip archive"
        )
    if manifest.get("format") == "rbfsafe-transparency-gossip-archive":
        required = (
            *transparency_arguments,
            args.expected_gossip_trust_bundle,
            args.expected_gossip_head,
            args.trust_history,
            args.trust_checkpoint,
            args.expected_trust_root,
            args.expected_trust_checkpoint,
        )
        if any(value is None for value in required):
            parser.error(
                "transparency-gossip audit requires the four --transparency-* "
                "identity options, --expected-gossip-trust-bundle, "
                "--expected-gossip-head, --trust-history, --trust-checkpoint, "
                "--expected-trust-root, and --expected-trust-checkpoint"
            )
        try:
            public_key = bytes.fromhex(args.transparency_public_key)
        except ValueError:
            parser.error("--transparency-public-key must be lowercase hexadecimal")
        if (
            len(public_key) != 32
            or args.transparency_public_key != args.transparency_public_key.lower()
        ):
            parser.error(
                "--transparency-public-key must contain 64 lowercase hexadecimal characters"
            )
        identity = TransparencyLogIdentity.create(
            args.transparency_namespace,
            args.transparency_service_id,
            args.transparency_key_id,
            public_key,
        )
        checkpoint = ServiceTrustCheckpoint.load(args.trust_checkpoint)
        history = ServiceTrustHistory.open(
            args.trust_history,
            args.expected_trust_root,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        trust_bundle = history.bundle(args.expected_gossip_trust_bundle)
        archive = TransparencyGossipArchive.open(
            args.atlas,
            identity,
            trust_bundle,
            args.expected_gossip_trust_bundle,
            args.expected_gossip_head,
        )
        audit = archive.audit()
        print("RBF-Safe transparency-gossip-archive schema=1")
        print(
            f"log={identity.id} trust_bundle={archive.trust_bundle_id} "
            f"trust_sequence={archive.trust_bundle_sequence}"
        )
        print(
            f"head={archive.current_record_id or '-'} records={len(archive.records)} "
            f"authenticated={audit.authenticated_gossip_count} "
            f"checkpoints={audit.unique_checkpoint_count}"
        )
        print(
            f"status={transparency_gossip_status_name(audit.status)} "
            f"linked_pairs={audit.linked_checkpoint_pairs} "
            f"unlinked_pairs={audit.unlinked_checkpoint_pairs} "
            f"conflicts={len(audit.conflicts)}"
        )
        for conflict in audit.conflicts:
            print(
                f"conflict={conflict.id} "
                f"type={transparency_gossip_conflict_type_name(conflict.type)} "
                f"first={conflict.first_checkpoint_id} "
                f"second={conflict.second_checkpoint_id}"
            )
        print("audit_evidence=unknown")
        print("runtime_executable=false")
        return 0
    if manifest.get("format") == "rbfsafe-transparency-log":
        log_arguments = (*transparency_arguments, args.expected_transparency_checkpoint)
        if any(value is None for value in log_arguments):
            parser.error(
                "transparency-log audit requires --transparency-namespace, "
                "--transparency-service-id, --transparency-key-id, "
                "--transparency-public-key, and --expected-transparency-checkpoint"
            )
        try:
            public_key = bytes.fromhex(args.transparency_public_key)
        except ValueError:
            parser.error("--transparency-public-key must be lowercase hexadecimal")
        if (
            len(public_key) != 32
            or args.transparency_public_key != args.transparency_public_key.lower()
        ):
            parser.error(
                "--transparency-public-key must contain 64 lowercase hexadecimal characters"
            )
        identity = TransparencyLogIdentity.create(
            args.transparency_namespace,
            args.transparency_service_id,
            args.transparency_key_id,
            public_key,
        )
        log = TransparencyLog.open(
            args.atlas,
            identity,
            args.expected_transparency_checkpoint,
        )
        audit = log.audit()
        print("RBF-Safe transparency-log schema=1")
        print(
            f"log={identity.id} namespace={identity.log_namespace} "
            f"checkpoint={audit.current_checkpoint_id or '-'}"
        )
        print(
            f"root={audit.current_root_hash or '-'} "
            f"records={audit.verified_records} "
            f"deployment_anchors={audit.deployment_anchor_count} "
            f"runtime_observations={audit.runtime_observation_count}"
        )
        for record in log.records:
            print(
                f"record={record.id} sequence={record.sequence} "
                f"kind={transparency_leaf_kind_name(record.leaf.kind)} "
                f"leaf={record.leaf.id} checkpoint={record.checkpoint.id}"
            )
        print("audit_evidence=unknown")
        print("runtime_executable=false")
        return 0
    if manifest.get("format") == "rbfsafe-execution-ledger":
        required = (
            args.execution_session,
            args.reviewed_profile,
            args.execution_atlas,
            args.trust_history,
            args.trust_checkpoint,
            args.expected_trust_root,
            args.expected_trust_checkpoint,
        )
        if any(value is None for value in required):
            parser.error(
                "execution-ledger audit requires --execution-session, --reviewed-profile, "
                "--execution-atlas, --trust-history, --trust-checkpoint, "
                "--expected-trust-root, and --expected-trust-checkpoint"
            )
        checkpoint = ServiceTrustCheckpoint.load(args.trust_checkpoint)
        history = ServiceTrustHistory.open(
            args.trust_history,
            args.expected_trust_root,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        reviewed = ReviewedDeploymentProfile.load(
            args.reviewed_profile,
            history,
            checkpoint,
            args.expected_trust_checkpoint,
        )
        atlas = SafeAtlas.load(args.execution_atlas)
        session = BoundedExecutionSession.load(
            args.execution_session,
            reviewed,
            history,
            checkpoint,
            args.expected_trust_checkpoint,
            atlas,
        )
        ledger = ExecutionLedger.open(
            args.atlas,
            session,
            reviewed,
            history,
            atlas,
        )
        audit = ledger.audit(session, reviewed, history, atlas)
        print("RBF-Safe execution-ledger schema=1")
        print(
            f"ledger={ledger.id} session={ledger.session_id} "
            f"head={ledger.current_record_id}"
        )
        print(
            f"status={execution_ledger_status_name(audit.status)} "
            f"records={audit.verified_records} "
            f"authorizations={audit.authorization_count} "
            f"completions={audit.completion_count}"
        )
        print(
            f"verified_checkpoints={audit.verified_checkpoints} "
            f"latest_checkpoint={audit.latest_checkpoint_id or '-'}"
        )
        print("audit_evidence=unknown")
        print("runtime_executable=false")
        return 0
    if manifest.get("format") == "rbfsafe-service-trust-history":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
            args.policy_confidence,
            args.calibration_profile,
            args.trust_history,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error(
                "Atlas, memory, fleet, policy, and query options do not apply "
                "to service trust histories"
            )
        if args.expected_trust_root is None:
            parser.error(
                "--expected-trust-root is required for a service trust history"
            )
        checkpoint_verified = False
        if args.trust_checkpoint is not None:
            if (
                args.expected_trust_checkpoint is None
                or args.expected_trust_head is not None
            ):
                parser.error(
                    "--trust-checkpoint requires --expected-trust-checkpoint "
                    "and cannot be combined with --expected-trust-head"
                )
            checkpoint = ServiceTrustCheckpoint.load(args.trust_checkpoint)
            history = ServiceTrustHistory.open(
                args.atlas,
                args.expected_trust_root,
                checkpoint,
                args.expected_trust_checkpoint,
            )
            checkpoint_verified = True
        else:
            if (
                args.expected_trust_head is None
                or args.expected_trust_checkpoint is not None
            ):
                parser.error(
                    "--expected-trust-head is required when no trust checkpoint "
                    "is supplied"
                )
            history = ServiceTrustHistory.open(
                args.atlas, args.expected_trust_root, args.expected_trust_head
            )
        print(
            f"RBF-Safe service-trust-history schema={history.storage_schema}"
        )
        print(
            f"records={len(history.records)} root={history.root_bundle_id} "
            f"head={history.current_bundle_id}"
        )
        for record in history.records:
            line = (
                f"rotation={record.id} sequence={record.sequence} "
                f"parent={record.parent_id or '-'} "
                f"type={service_trust_rotation_event_type_name(record.type)} "
                f"bundle={record.bundle_id}"
            )
            if record.authorization is not None:
                line += (
                    f" authorization={record.authorization.id} "
                    f"signer_service={record.authorization.signer_service_id} "
                    f"signer_key={record.authorization.signer_key_id}"
                )
            print(line)
            if record.authorization_set is not None:
                print(
                    f"  authorization_set={record.authorization_set.id} "
                    f"signatures={len(record.authorization_set.authorizations)}"
                )
                for authorization in record.authorization_set.authorizations:
                    print(
                        f"  signer_service={authorization.signer_service_id} "
                        f"signer_key={authorization.signer_key_id}"
                    )
        print("caller_pinned=true")
        print("expected_head_verified=true")
        print(f"checkpoint_verified={str(checkpoint_verified).lower()}")
        print("runtime_executable=false")
        return 0
    if any(value is not None for value in trust_arguments):
        parser.error(
            "trust-root, head, history, and checkpoint options require a service "
            "trust history or checkpoint"
        )
    if manifest.get("format") == "rbfsafe-artifact-transfer-journal":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
            args.fleet_schedule_version,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error(
                "Atlas, memory, fleet, policy, and query options do not apply "
                "to artifact transfer journals"
            )
        journal = ArtifactTransferJournal.load(args.atlas)
        print(
            "RBF-Safe artifact-transfer-journal "
            f"schema={manifest.get('schema', '?')}"
        )
        print(
            f"records={len(journal.records)} current={journal.current_record_id or '-'} "
            f"identity={journal.identity}"
        )
        if journal.records:
            record = journal.records[-1]
            transfer = record.transfer
            print(
                f"latest={record.id} sequence={record.sequence} parent={record.parent_id or '-'} "
                f"operation={artifact_transfer_operation_name(transfer.operation)}"
            )
            print(
                f"transfer={transfer.id} service={transfer.service_id} "
                f"artifact={transfer.artifact_id} generation={transfer.artifact_generation}"
            )
            print(
                f"payload={transfer.payload_digest} bytes={transfer.payload_bytes} "
                f"authentication={artifact_transfer_authentication_name(transfer.authentication)}"
            )
            if transfer.verification_key_id:
                print(
                    f"verification_key={transfer.verification_key_id} "
                    f"trust_bundle={transfer.trust_bundle_id}"
                )
        print("runtime_executable=false")
        return 0
    if manifest.get("format") == "rbfsafe-fleet-schedule-archive":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.task_id,
            args.episode_id,
            args.feedback_label,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error("Atlas, policy-feedback, and safety-memory options do not apply to fleet schedules")
        archive = FleetScheduleArchive.load(args.atlas)
        print("RBF-Safe fleet-schedule-archive schema=1")
        print(
            f"fleet={archive.fleet_id} versions={len(archive.versions)} "
            f"current={archive.current_version_id}"
        )
        if args.fleet_schedule_version is not None:
            version = archive.version(args.fleet_schedule_version)
        elif archive.current_version_id:
            version = archive.current_version()
        else:
            return 0
        report = version.report
        print(
            f"selected={version.id} sequence={version.sequence} parent={version.parent_id} "
            f"memory={version.memory_id} snapshot={version.fleet.id}"
        )
        print(
            f"status={fleet_schedule_status_name(report.status)} "
            f"reservations={len(report.reservations)} conflicts={len(report.conflicts)} "
            f"pair_evaluations={report.pair_evaluations}"
        )
        return 0
    if args.fleet_schedule_version is not None:
        parser.error("--fleet-schedule-version requires a fleet-schedule archive")
    if manifest.get("format") == "rbfsafe-safety-memory-store":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.episode_id,
            args.feedback_label,
        )
        if any(value is not None for value in unsupported) or args.include_portals or args.include_tubes:
            parser.error("Atlas, policy-feedback, trajectory, and Safe IK options do not apply to safety memory")
        store = SafetyMemoryStore.open(args.atlas)
        memory = (
            store.load_revision(args.memory_revision)
            if args.memory_revision is not None
            else store.load_current()
        )
        selected_revision = args.memory_revision or store.current_revision_id
        print("RBF-Safe safety-memory-store schema=1")
        print(
            f"revisions={len(store.revisions)} current={store.current_revision_id} "
            f"selected={selected_revision}"
        )
        _print_safety_memory(memory, args, parser)
        return 0
    if manifest.get("format") == "rbfsafe-safety-memory":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.policy_id,
            args.episode_id,
            args.feedback_label,
            args.memory_revision,
        )
        if any(value is not None for value in unsupported) or args.include_portals or args.include_tubes:
            parser.error("Atlas, policy-feedback, trajectory, and Safe IK options do not apply to safety memory")
        memory = SafetyMemory.load(args.atlas)
        print("RBF-Safe safety-memory schema=1")
        _print_safety_memory(memory, args, parser)
        return 0
    if manifest.get("format") == "rbfsafe-policy-feedback":
        unsupported = (
            args.plot,
            args.query,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
            args.deployment_id,
            args.memory_state,
            args.artifact_type,
            args.memory_revision,
        )
        if (
            any(value is not None for value in unsupported)
            or args.include_portals
            or args.include_tubes
            or args.include_memory_events
        ):
            parser.error("Atlas, region, update, trajectory, and Safe IK options do not apply to policy feedback")
        if args.max_feedback_results <= 0:
            parser.error("--max-feedback-results must be positive")
        database = PolicyFeedbackDatabase.load(args.atlas)
        query = PolicyFeedbackQuery()
        query.policy_id = args.policy_id or ""
        query.task_id = args.task_id or ""
        query.episode_id = args.episode_id or ""
        query.maximum_results = args.max_feedback_results
        labels = {
            "selected_accepted": PolicyFeedbackLabel.SELECTED_ACCEPTED,
            "selected_repaired": PolicyFeedbackLabel.SELECTED_REPAIRED,
            "eligible_not_selected": PolicyFeedbackLabel.ELIGIBLE_NOT_SELECTED,
            "policy_rejected": PolicyFeedbackLabel.POLICY_REJECTED,
            "shield_rejected": PolicyFeedbackLabel.SHIELD_REJECTED,
        }
        if args.feedback_label is not None:
            query.label = labels[args.feedback_label]
        records = database.query(query)
        summary = database.summary
        print("RBF-Safe policy-feedback schema=1")
        print(
            f"records={summary.records} selected_accepted={summary.selected_accepted} "
            f"selected_repaired={summary.selected_repaired} "
            f"eligible_not_selected={summary.eligible_not_selected} "
            f"policy_rejected={summary.policy_rejected} shield_rejected={summary.shield_rejected}"
        )
        print(f"query_records={len(records)}")
        for record in records:
            print(
                f"feedback={record.id} policy={record.metadata.policy_id} "
                f"task={record.metadata.task_id} episode={record.metadata.episode_id} "
                f"sequence={record.metadata.sequence} label={policy_feedback_label_name(record.label)}"
            )
        return 0
    if any(value is not None for value in feedback_filters):
        parser.error("policy feedback filters require a policy feedback database")
    if any(value is not None for value in memory_filters) or args.include_memory_events:
        parser.error("safety-memory filters require a safety memory database")
    if manifest.get("format") == "rbfsafe-region-database":
        unsupported = (
            args.plot,
            args.trajectory,
            args.robot,
            args.scene,
            args.ik_target,
            args.seed,
            args.previous_scene,
            args.next_scene,
            args.update_output,
            args.repair_samples,
            args.store_version,
            args.publish_atlas,
            args.rollback_version,
        )
        if any(value is not None for value in unsupported):
            parser.error("update, store, plot, trajectory, and Safe IK options do not apply to region databases")
        database = RegionDatabase.load(args.atlas)
        counts = {
            RegionType.AABB: 0,
            RegionType.OBB: 0,
            RegionType.PORTAL: 0,
            RegionType.TRAJECTORY_TUBE: 0,
            RegionType.ZONOTOPE: 0,
            RegionType.TAYLOR: 0,
        }
        for record in database.records:
            counts[record.type] += 1
        components = {record.component for record in database.records if record.component != 0}
        print(f"RBF-Safe region-database schema=1 dimension={database.dimension}")
        print(
            f"records={len(database.records)} certificates={len(database.certificates)} "
            f"components={len(components)}"
        )
        print(
            "types="
            + ",".join(
                f"{name}:{counts[value]}"
                for name, value in (
                    ("aabb", RegionType.AABB),
                    ("obb", RegionType.OBB),
                    ("portal", RegionType.PORTAL),
                    ("trajectory_tube", RegionType.TRAJECTORY_TUBE),
                    ("zonotope", RegionType.ZONOTOPE),
                    ("taylor", RegionType.TAYLOR),
                )
            )
        )
        print(f"robot={database.robot_digest}")
        print(f"scene={database.scene_digest} version={database.scene_version}")
        if args.query is not None:
            if len(args.query) != database.dimension or not all(
                math.isfinite(value) for value in args.query
            ):
                parser.error(f"--query requires {database.dimension} coordinates")
            query_options = RegionQueryOptions()
            query_options.include_portals = args.include_portals
            query_options.include_trajectory_tubes = args.include_tubes
            regions = database.regions_at(args.query, query_options)
            nearest = database.nearest_region(args.query, query_options)
            print(f"query_contains={str(bool(regions)).lower()}")
            print("query_regions=" + ",".join(str(region.id) for region in regions))
            if nearest is not None:
                print(f"nearest_region={nearest.id}")
        return 0
    if manifest.get("format") == "rbfsafe-corridor":
        if (
            args.plot is not None
            or args.trajectory is not None
            or args.robot is not None
            or args.scene is not None
            or args.ik_target is not None
            or args.seed is not None
            or args.previous_scene is not None
            or args.next_scene is not None
            or args.update_output is not None
            or args.repair_samples is not None
            or args.store_version is not None
            or args.publish_atlas is not None
            or args.rollback_version is not None
            or args.include_portals
            or args.include_tubes
        ):
            parser.error("update, store, plot, trajectory, and Safe IK options do not apply to corridors")
        corridor = HipacCorridor.load(args.atlas)
        components = {region.component for region in corridor.regions}
        print(f"RBF-Safe corridor schema=1 dimension={corridor.dimension}")
        print(
            f"regions={len(corridor.regions)} portals={len(corridor.portals)} "
            f"components={len(components)}"
        )
        print(f"robot={corridor.robot_digest}")
        print(f"scene={corridor.scene_digest}")
        if args.query is not None:
            if len(args.query) != corridor.dimension or not all(
                math.isfinite(value) for value in args.query
            ):
                parser.error(f"--query requires {corridor.dimension} coordinates")
            regions = corridor.regions_at(args.query)
            print(f"query_contains={str(bool(regions)).lower()}")
            print("query_regions=" + ",".join(str(region) for region in regions))
        return 0

    store = None
    if args.include_portals or args.include_tubes:
        parser.error("--include-portals and --include-tubes require a region database")
    if store_manifest.get("format") == "rbfsafe-atlas-version-store":
        store = AtlasVersionStore.open(args.atlas)
        if args.publish_atlas is not None:
            store.publish(SafeAtlas.load(args.publish_atlas))
        if args.rollback_version is not None:
            store.rollback(args.rollback_version)
        atlas = (
            store.load_version(args.store_version)
            if args.store_version is not None
            else store.load_current()
        )
        print(
            f"RBF-Safe version-store versions={len(store.versions)} "
            f"current={store.current_version_id}"
        )
    else:
        if (
            args.store_version is not None
            or args.publish_atlas is not None
            or args.rollback_version is not None
        ):
            parser.error("--store-version, --publish-atlas, and --rollback-version require a version store")
        atlas = SafeAtlas.load(args.atlas)

    update_arguments = (args.previous_scene, args.next_scene, args.update_output)
    if any(argument is not None for argument in update_arguments):
        if args.robot is None or not all(argument is not None for argument in update_arguments):
            parser.error(
                "--robot, --previous-scene, --next-scene, and --update-output must be used together"
            )
        repair_samples = []
        if args.repair_samples is not None:
            try:
                repair_samples = json.loads(args.repair_samples.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                parser.error(f"cannot read --repair-samples: {error}")
            if not isinstance(repair_samples, list):
                parser.error("--repair-samples must contain a JSON array")
        robot = SerialRobotModel.from_json(args.robot)
        previous_scene = SceneSnapshot.from_json(args.previous_scene)
        next_scene = SceneSnapshot.from_json(args.next_scene)
        update = AtlasUpdater().update(robot, previous_scene, next_scene, atlas, repair_samples)
        update.atlas.save(args.update_output)
        atlas = update.atlas
        print(
            f"update_delta={update.delta.digest} inherited={update.stats.certificates_inherited} "
            f"revalidated={update.stats.regions_revalidated} "
            f"invalidated={update.stats.regions_invalidated} repaired={update.stats.repaired_regions}"
        )
    elif args.repair_samples is not None:
        parser.error("--repair-samples requires an incremental update")

    components = {region.component for region in atlas.regions}
    print(f"RBF-Safe atlas schema={atlas.storage_schema} dimension={atlas.dimension}")
    print(f"regions={len(atlas.regions)} certificates={len(atlas.certificates)} components={len(components)}")
    print(
        f"lect_nodes={atlas.lect.size} repair_domains={len(atlas.repair_domains)} "
        f"version={atlas.version_info.id} sequence={atlas.version_info.sequence}"
    )
    print(f"robot={atlas.robot_digest}")
    print(f"scene={atlas.scene_digest}")
    if args.query is not None:
        if len(args.query) != atlas.dimension or not all(math.isfinite(value) for value in args.query):
            parser.error(f"--query requires {atlas.dimension} coordinates")
        regions = atlas.regions_at(args.query)
        nearest = atlas.nearest_region(args.query)
        print(f"query_contains={str(bool(regions)).lower()}")
        print("query_regions=" + ",".join(str(region.id) for region in regions))
        if nearest is not None:
            print(f"nearest_region={nearest.id}")
    if args.trajectory is not None:
        if args.max_region_tests <= 0:
            parser.error("--max-region-tests must be positive")
        try:
            document = json.loads(args.trajectory.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            parser.error(f"cannot read --trajectory: {error}")
        waypoints = document.get("waypoints") if isinstance(document, dict) else document
        if not isinstance(waypoints, list) or len(waypoints) < 2:
            parser.error("--trajectory must contain at least two waypoints")
        for index, waypoint in enumerate(waypoints):
            if (
                not isinstance(waypoint, list)
                or len(waypoint) != atlas.dimension
                or not all(
                    isinstance(value, (int, float))
                    and not isinstance(value, bool)
                    and math.isfinite(value)
                    for value in waypoint
                )
            ):
                parser.error(f"trajectory waypoint {index} requires {atlas.dimension} finite coordinates")
        audit_options = TrajectoryAuditOptions()
        audit_options.maximum_region_tests = args.max_region_tests
        report = TrajectoryAuditor().audit(atlas, waypoints, audit_options)
        status_names = {
            TrajectoryAuditStatus.CERTIFIED: "CERTIFIED",
            TrajectoryAuditStatus.PARTIAL: "PARTIAL",
            TrajectoryAuditStatus.INVALID: "INVALID",
        }
        print(f"trajectory_status={status_names[report.status]}")
        print(f"trajectory_coverage={report.coverage_ratio:.12g}")
        print("trajectory_regions=" + ",".join(str(region) for region in report.region_sequence))
        print(
            "trajectory_uncovered="
            + ";".join(
                f"{interval.segment_index}:"
                f"{'[' if interval.start_included else '('}"
                f"{interval.start_fraction:.12g},{interval.end_fraction:.12g}"
                f"{']' if interval.end_included else ')'}"
                for interval in report.uncovered_intervals
            )
        )
    ik_arguments = (args.scene, args.ik_target, args.seed)
    if any(argument is not None for argument in ik_arguments):
        if args.robot is None or not all(argument is not None for argument in ik_arguments):
            parser.error("--robot, --scene, --ik-target, and --seed must be used together")
        if len(args.seed) != atlas.dimension or not all(math.isfinite(value) for value in args.seed):
            parser.error(f"--seed requires {atlas.dimension} finite coordinates")
        if not all(math.isfinite(value) for value in args.ik_target):
            parser.error("--ik-target requires seven finite coordinates")
        robot = SerialRobotModel.from_json(args.robot)
        scene = SceneSnapshot.from_json(args.scene)
        atlas.verify_compatible(robot, scene)
        target = Pose3d(args.ik_target[:3], args.ik_target[3:])
        report = SafeIkSolver().solve(robot, scene, atlas, target, args.seed)
        status_names = {
            SafeIkStatus.SAFE_CONNECTED: "SAFE_CONNECTED",
            SafeIkStatus.SAFE_UNCONNECTED: "SAFE_UNCONNECTED",
            SafeIkStatus.SEED_NOT_CERTIFIED: "SEED_NOT_CERTIFIED",
            SafeIkStatus.NO_SOLUTION: "NO_SOLUTION",
        }
        print(f"safe_ik_status={status_names[report.status]}")
        print("safe_ik_solution=" + ",".join(f"{value:.12g}" for value in report.solution))
        print(f"safe_ik_region={report.region_id}")
        print(f"safe_ik_position_error={report.position_error:.12g}")
        print(f"safe_ik_orientation_error={report.orientation_error:.12g}")
        if report.connectivity_route is not None:
            print(
                "safe_ik_route="
                + ",".join(str(region) for region in report.connectivity_route.region_sequence)
            )
            print(f"safe_ik_connectivity_certificate={report.connectivity_route.certificate.id}")
        if report.status != SafeIkStatus.SAFE_CONNECTED:
            return 3
    if args.plot:
        from .visualize import plot_slice

        figure = plot_slice(atlas, tuple(args.dims), args.fixed)
        figure.savefig(args.plot, bbox_inches="tight", dpi=160)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
