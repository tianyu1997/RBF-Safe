"""Inspect and optionally visualize RBF-Safe certificate databases."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from . import (
    AtlasUpdater,
    AtlasVersionStore,
    FleetScheduleArchive,
    HipacCorridor,
    MemoryArtifactState,
    MemoryArtifactType,
    PolicyFeedbackDatabase,
    PolicyFeedbackLabel,
    PolicyFeedbackQuery,
    Pose3d,
    RegionDatabase,
    RegionQueryOptions,
    RegionType,
    SafeAtlas,
    SafeIkSolver,
    SafeIkStatus,
    SafetyMemory,
    SafetyMemoryStore,
    SceneSnapshot,
    SerialRobotModel,
    TrajectoryAuditor,
    TrajectoryAuditOptions,
    TrajectoryAuditStatus,
    fleet_schedule_status_name,
    policy_feedback_label_name,
    memory_artifact_state_name,
    memory_artifact_type_name,
    memory_event_type_name,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="rbfsafe-inspect")
    parser.add_argument(
        "atlas",
        type=Path,
        help="Atlas, safety memory, fleet schedule, version store, corridor, or region database directory",
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
