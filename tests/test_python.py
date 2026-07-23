from __future__ import annotations

import json
from pathlib import Path

import pytest
import rbfsafe


def test_version() -> None:
    assert rbfsafe.__version__ == "3.1.0"


def make_robot() -> rbfsafe.SerialRobotModel:
    return rbfsafe.SerialRobotModel(
        "python-planar-2r",
        [
            rbfsafe.DhJoint(0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE),
            rbfsafe.DhJoint(0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE),
        ],
        [rbfsafe.Interval(-1.5, 1.5), rbfsafe.Interval(-1.5, 1.5)],
        [0.05, 0.05],
    )


def test_end_to_end(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-empty-v1")
    result = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0], [1.0, -1.0]])
    assert result.atlas.contains([0.0, 0.0])
    assert result.atlas.connected([0.0, 0.0], [1.0, -1.0])
    route = result.atlas.route([0.0, 0.0], [1.0, -1.0])
    assert route is not None
    assert route.certificate.level == rbfsafe.EvidenceLevel.CERTIFIED_CONNECTIVITY
    assert result.atlas.certificates[0].level == rbfsafe.EvidenceLevel.CERTIFIED_REGION
    destination = tmp_path / "atlas"
    result.atlas.save(destination)
    loaded = rbfsafe.SafeAtlas.load(destination)
    loaded.verify_compatible(robot, scene)
    assert loaded.robot_digest == robot.digest
    assert loaded.regions[0].id == result.atlas.regions[0].id
    from rbfsafe.cli import main

    assert main([str(destination), "--query", "0.0", "0.0"]) == 0
    assert "query_contains=true" in capsys.readouterr().out


def test_safe_ik() -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-safe-ik-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas
    target = robot.end_effector_pose([0.4, -0.2])
    assert target.valid()
    report = rbfsafe.SafeIkSolver().solve(robot, scene, atlas, target, [0.0, 0.0])
    assert report.status == rbfsafe.SafeIkStatus.SAFE_CONNECTED
    assert report.pose_evidence == rbfsafe.EvidenceLevel.POINT_CHECKED
    assert report.position_error <= 1e-4
    assert report.orientation_error <= 1e-3
    assert report.region_certificate.level == rbfsafe.EvidenceLevel.CERTIFIED_REGION
    assert report.connectivity_route.certificate.level == rbfsafe.EvidenceLevel.CERTIFIED_CONNECTIVITY


def test_dynamic_update_and_version_store(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    robot = rbfsafe.SerialRobotModel(
        "python-dynamic-prismatic",
        [rbfsafe.DhJoint(0.0, 0.0, 0.0, 0.0, rbfsafe.JointType.PRISMATIC)],
        [rbfsafe.Interval(0.0, 2.0)],
        [0.05],
    )
    empty = rbfsafe.SceneSnapshot([], "python-dynamic-empty-v1")
    blocked = rbfsafe.SceneSnapshot(
        [
            rbfsafe.SceneObstacle(
                "block", rbfsafe.WorkspaceAabb([-0.1, -0.1, 1.1], [0.1, 0.1, 1.2])
            )
        ],
        "python-dynamic-blocked-v1",
    )
    initial = rbfsafe.AtlasBuilder().build(robot, empty, [[0.25]]).atlas
    assert initial.storage_schema == 2
    assert initial.version_info.sequence == 0
    assert len(initial.dependencies[0].envelope.links) == 1
    delta = rbfsafe.compare_scenes(empty, blocked)
    assert delta.geometry_changed
    assert delta.changes[0].kind == rbfsafe.SceneChangeKind.ADDED

    update = rbfsafe.AtlasUpdater().update(robot, empty, blocked, initial)
    assert update.stats.regions_invalidated == 1
    assert update.stats.repaired_regions == 1
    assert update.atlas.contains([0.25])
    assert not update.atlas.contains([1.5])
    assert len(update.atlas.repair_domains) == 1
    assert update.atlas.version_info.parent_id == initial.version_info.id

    reopened = rbfsafe.SceneSnapshot([], "python-dynamic-empty-v2")
    recovered = rbfsafe.AtlasUpdater().update(robot, blocked, reopened, update.atlas)
    assert recovered.atlas.contains([1.5])
    assert not recovered.atlas.repair_domains

    store = rbfsafe.AtlasVersionStore.create(tmp_path / "versions", initial)
    store.publish(update.atlas)
    assert len(store.versions) == 2
    assert store.load_current().scene_digest == blocked.digest
    store.rollback(initial.version_info.id)
    assert store.load_current().scene_digest == empty.digest
    reopened_store = rbfsafe.AtlasVersionStore.open(tmp_path / "versions")
    assert reopened_store.current_version_id == initial.version_info.id

    initial_path = tmp_path / "initial-atlas"
    initial.save(initial_path)
    robot_path = tmp_path / "dynamic-robot.json"
    previous_scene_path = tmp_path / "previous-scene.json"
    next_scene_path = tmp_path / "next-scene.json"
    update_path = tmp_path / "cli-update"
    robot_path.write_text(
        json.dumps(
            {
                "schema": 1,
                "name": "python-dynamic-prismatic",
                "joints": [
                    {
                        "alpha": 0.0,
                        "a": 0.0,
                        "d": 0.0,
                        "theta": 0.0,
                        "type": "prismatic",
                    }
                ],
                "joint_limits": [[0.0, 2.0]],
                "link_radii": [0.05],
                "tool_frame": None,
            }
        ),
        encoding="utf-8",
    )
    previous_scene_path.write_text(
        json.dumps({"schema": 1, "version": empty.version, "obstacles": []}),
        encoding="utf-8",
    )
    next_scene_path.write_text(
        json.dumps(
            {
                "schema": 1,
                "version": blocked.version,
                "obstacles": [
                    {
                        "id": "block",
                        "lower": [-0.1, -0.1, 1.1],
                        "upper": [0.1, 0.1, 1.2],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    from rbfsafe.cli import main

    assert (
        main(
            [
                str(initial_path),
                "--robot",
                str(robot_path),
                "--previous-scene",
                str(previous_scene_path),
                "--next-scene",
                str(next_scene_path),
                "--update-output",
                str(update_path),
            ]
        )
        == 0
    )
    assert "invalidated=1 repaired=1" in capsys.readouterr().out
    assert rbfsafe.SafeAtlas.load(update_path).scene_digest == blocked.digest
    assert main([str(tmp_path / "versions")]) == 0
    assert "RBF-Safe version-store versions=2" in capsys.readouterr().out


def test_safe_ik_cli(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    repository = Path(__file__).resolve().parents[1]
    robot_path = repository / "data" / "planar_2r.json"
    scene_path = repository / "data" / "empty_scene.json"
    robot = rbfsafe.SerialRobotModel.from_json(robot_path)
    scene = rbfsafe.SceneSnapshot.from_json(scene_path)
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas
    destination = tmp_path / "safe-ik-atlas"
    atlas.save(destination)
    target = robot.end_effector_pose([0.4, -0.2])

    from rbfsafe.cli import main

    arguments = [
        str(destination),
        "--robot",
        str(robot_path),
        "--scene",
        str(scene_path),
        "--ik-target",
        *(str(value) for value in (*target.position, *target.orientation)),
        "--seed",
        "0.0",
        "0.0",
    ]
    assert main(arguments) == 0
    output = capsys.readouterr().out
    assert "safe_ik_status=SAFE_CONNECTED" in output
    assert "safe_ik_connectivity_certificate=" in output


def test_public_lect(tmp_path: Path) -> None:
    tree = rbfsafe.LectTree.create(
        rbfsafe.CspaceAabb([rbfsafe.Interval(-1.0, 1.0), rbfsafe.Interval(-2.0, 2.0)])
    )
    children = tree.split(rbfsafe.LectNodeKey(""))
    assert children[0].path == "0"
    assert tree.locate([-0.5, 0.0]).path == "0"
    destination = tmp_path / "lect"
    tree.save(destination)
    snapshot = rbfsafe.LectSnapshot.open(destination)
    assert snapshot.size == tree.size


def test_hipac_corridor(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-hipac-v1")
    options = rbfsafe.HipacOptions()
    options.minimum_lateral_half_width = 0.01
    options.maximum_lateral_half_width = 0.05
    growth_options = rbfsafe.ObbGrowthOptions()
    growth_options.initial_lateral_half_width = 0.01
    growth_options.maximum_lateral_half_width = 0.05
    grown = rbfsafe.ObbGrower().grow(
        robot, scene, [-0.5, -0.5], [0.5, 0.5], growth_options
    )
    assert grown.certified
    assert grown.validation.disposition == rbfsafe.ValidationDisposition.CERTIFIED_FREE
    assert grown.achieved_lateral_half_width == pytest.approx(0.05)
    report = rbfsafe.HipacCorridorBuilder().build(
        robot, scene, [[-1.0, -1.0], [0.0, 0.0], [1.0, 1.0]], options
    )
    assert report.status == rbfsafe.HipacBuildStatus.CERTIFIED
    assert report.coverage_ratio == pytest.approx(1.0)
    assert len(report.corridor.regions) == 2
    assert len(report.corridor.portals) == 1
    assert report.corridor.connected([-0.5, -0.5], [0.5, 0.5])
    route = report.corridor.route([-0.5, -0.5], [0.5, 0.5])
    assert route is not None
    assert route.certificate.level == rbfsafe.EvidenceLevel.CERTIFIED_CONNECTIVITY
    assert len(route.waypoints) == 3
    destination = tmp_path / "corridor"
    report.corridor.save(destination)
    loaded = rbfsafe.HipacCorridor.load(destination)
    loaded.verify_compatible(robot, scene)
    assert loaded.connected([-0.5, -0.5], [0.5, 0.5])
    from rbfsafe.cli import main

    assert main([str(destination), "--query", "-0.5", "-0.5"]) == 0
    assert "RBF-Safe corridor" in capsys.readouterr().out


def test_region_database_and_higher_order(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-region-database-v1")
    options = rbfsafe.ObbAtlasBuildOptions()
    options.initial_half_width = 0.01
    options.maximum_half_width = 0.05
    result = rbfsafe.ObbAtlasBuilder().build(
        robot, scene, [[-0.5, -0.2], [0.0, 0.0], [0.5, 0.2]], options
    )
    database = result.database
    assert database.contains([0.0, 0.0])
    assert database.connected([-0.5, -0.2], [0.5, 0.2])
    assert any(record.type == rbfsafe.RegionType.PORTAL for record in database.records)
    destination = tmp_path / "region-database"
    database.save(destination)
    loaded = rbfsafe.RegionDatabase.load(destination)
    loaded.verify_compatible(robot, scene)
    assert [record.id for record in loaded.records] == [record.id for record in database.records]

    portal_options = rbfsafe.RegionQueryOptions()
    portal_options.include_portals = True
    assert len(loaded.regions_at([0.0, 0.0], portal_options)) >= len(
        loaded.regions_at([0.0, 0.0])
    )
    from rbfsafe.cli import main

    assert main([str(destination), "--query", "0.0", "0.0", "--include-portals"]) == 0
    assert "RBF-Safe region-database" in capsys.readouterr().out

    zonotope = rbfsafe.CspaceZonotope([0.0, 0.0], 1, [0.15, -0.15])
    validator = rbfsafe.HigherOrderRegionValidator()
    validation = validator.validate(robot, scene, zonotope)
    assert validation.disposition == rbfsafe.ValidationDisposition.CERTIFIED_FREE
    certificate = rbfsafe.make_higher_order_region_certificate(
        robot, scene, zonotope, validator, validation
    )
    higher_order = rbfsafe.RegionDatabase.create(
        robot,
        scene,
        [
            rbfsafe.CertifiedRegionInput(
                zonotope, certificate, validation.envelope, "python-correlated-seed"
            )
        ],
    )
    assert higher_order.contains([0.1, -0.1])
    assert not higher_order.contains([0.1, 0.1])
    assert higher_order.records[0].type == rbfsafe.RegionType.ZONOTOPE


def test_certified_planning_and_optimization_consumers() -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-planning-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas

    sampler_options = rbfsafe.CertifiedSamplerOptions()
    sampler_options.seed = 123
    first_sampler = rbfsafe.CertifiedRegionSampler.create(atlas, sampler_options)
    second_sampler = rbfsafe.CertifiedRegionSampler.create(atlas, sampler_options)
    for _ in range(8):
        first = first_sampler.sample()
        second = second_sampler.sample()
        assert first == second
        assert atlas.contains(first)
    near = first_sampler.sample_near([0.0, 0.0], 0.1)
    assert atlas.contains(near)
    assert sum(value * value for value in near) <= 0.1**2 + 1e-12
    assert first_sampler.stats.samples_returned == 9

    roadmap_result = rbfsafe.CertifiedRoadmapBuilder().build(atlas)
    assert roadmap_result.roadmap.valid()
    assert roadmap_result.stats.region_nodes == 1
    assert roadmap_result.stats.portal_nodes == 0
    assert roadmap_result.roadmap.nearest_node([0.1, 0.1]) is not None
    roadmap_result.roadmap.verify_compatible(robot, scene)

    database = rbfsafe.RegionDatabase.from_atlas(atlas, scene.version)
    trajectory = [[-1.0, 1.0], [0.0, 0.0], [1.0, -1.0]]
    assignment = rbfsafe.assign_trajectory_regions(database, trajectory)
    assert assignment.status == rbfsafe.TrajectoryAssignmentStatus.COMPLETE
    constraint = rbfsafe.compile_region_constraint(database, assignment.region_ids[0])
    assert constraint.valid()
    assert constraint.evaluate([0.0, 0.0]).satisfied
    assert not constraint.evaluate([2.0, 2.0]).satisfied
    projection = constraint.project([2.0, 2.0])
    assert projection.converged
    assert database.contains(projection.configuration)

    program = rbfsafe.TrajOptRegionAdapter().compile(database, assignment.region_ids)
    assert program.backend == rbfsafe.OptimizationBackend.TRAJOPT
    assert rbfsafe.evaluate_trajectory_constraints(program, trajectory).satisfied
    projected = rbfsafe.project_trajectory_constraints(
        program, [[-2.0, 2.0], [0.0, 0.0], [2.0, -2.0]]
    )
    assert all(stage.converged for stage in projected)
    assert rbfsafe.ChompRegionAdapter().compile(database, assignment.region_ids).valid()
    assert rbfsafe.StompRegionAdapter().compile(database, assignment.region_ids).valid()
    assert rbfsafe.MpcRegionAdapter().compile(database, assignment.region_ids).valid()


def test_runtime_shield_batch_telemetry_and_monitor() -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-shield-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas
    shield = rbfsafe.RuntimeShield()

    accepted = shield.check_joint_action(
        robot,
        scene,
        atlas,
        [0.0, 0.0],
        rbfsafe.JointDeltaAction([0.1, -0.05]),
    )
    assert accepted.outcome == rbfsafe.ShieldOutcome.ACCEPT
    assert accepted.reason == rbfsafe.ShieldReason.CERTIFIED
    assert accepted.audit.status == rbfsafe.TrajectoryAuditStatus.CERTIFIED
    assert accepted.evidence == rbfsafe.EvidenceLevel.CERTIFIED_CONNECTIVITY
    assert accepted.evidence != rbfsafe.EvidenceLevel.RUNTIME_EXECUTABLE

    trajectory = shield.check_action(
        robot,
        scene,
        atlas,
        [0.0, 0.0],
        rbfsafe.TrajectoryAction([[0.2, -0.1], [0.4, -0.2]]),
    )
    assert trajectory.outcome == rbfsafe.ShieldOutcome.ACCEPT

    options = rbfsafe.ShieldBatchOptions()
    options.action.maximum_waypoint_repair_distance = 0.6
    options.action.maximum_total_repair_distance = 1.0
    batch = shield.check_actions(
        robot,
        scene,
        atlas,
        [0.0, 0.0],
        [
            rbfsafe.JointDeltaAction([2.0, 0.0]),
            rbfsafe.JointDeltaAction([0.1, 0.0]),
        ],
        options,
    )
    assert batch.selected_index == 1
    assert batch.decisions[0].outcome == rbfsafe.ShieldOutcome.REPAIR
    assert batch.decisions[1].outcome == rbfsafe.ShieldOutcome.ACCEPT
    assert shield.telemetry.total_actions == 4
    assert shield.telemetry.batches == 1

    monitor_options = rbfsafe.RuntimeMonitorOptions()
    monitor_options.tracking_tolerance = 0.05
    monitor = rbfsafe.RuntimeShieldMonitor(atlas, monitor_options)
    monitor.arm(accepted)
    assert monitor.observe([0.05, -0.025], 1.0).state == rbfsafe.MonitorState.ON_CERTIFIED_PLAN
    deviation = monitor.observe([1.0, 1.0], 2.0)
    assert deviation.state == rbfsafe.MonitorState.CERTIFIED_DEVIATION
    assert deviation.evidence == rbfsafe.EvidenceLevel.CERTIFIED_REGION
    assert monitor.observe([2.0, 2.0], 3.0).state == rbfsafe.MonitorState.UNCERTIFIED_STATE
    assert monitor.stats.observations == 3


def test_learning_policy_gate_feedback_and_cli(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-policy-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas

    def proposal_metadata(
        policy_id: str, sequence: int, confidence: float
    ) -> rbfsafe.PolicyProposalMetadata:
        metadata = rbfsafe.PolicyProposalMetadata()
        metadata.policy_id = policy_id
        metadata.task_id = "shelf-pick"
        metadata.episode_id = "episode-4"
        metadata.sequence = sequence
        metadata.confidence = confidence
        metadata.state_uncertainty = 0.05
        metadata.action_uncertainty = 0.04
        metadata.observation_age_seconds = 0.01
        metadata.inference_latency_seconds = 0.02
        return metadata

    proposals = [
        rbfsafe.PolicyProposal(
            rbfsafe.JointDeltaAction([0.1, 0.0]),
            proposal_metadata("vla-primary", 1, 0.95),
        ),
        rbfsafe.PolicyProposal(
            rbfsafe.JointDeltaAction([0.05, 0.0]),
            proposal_metadata("vla-fallback", 2, 0.8),
        ),
        rbfsafe.PolicyProposal(
            rbfsafe.JointDeltaAction([0.05, 0.0]),
            proposal_metadata("vla-low-confidence", 3, 0.2),
        ),
    ]
    options = rbfsafe.PolicyGateOptions()
    options.minimum_confidence = 0.7
    options.maximum_state_uncertainty = 0.2
    options.maximum_action_uncertainty = 0.2
    options.maximum_observation_age_seconds = 0.1
    options.maximum_inference_latency_seconds = 0.1
    options.selection_mode = rbfsafe.PolicySelectionMode.HIGHEST_CONFIDENCE

    gate = rbfsafe.LearningPolicySafetyGate()
    report = gate.check_proposals(robot, scene, atlas, [0.0, 0.0], proposals, options)
    assert report.selected_index == 0
    assert report.decisions[0].selected
    assert report.decisions[0].reason == rbfsafe.PolicyGateReason.SHIELD_ACCEPTED
    assert report.feedback[0].label == rbfsafe.PolicyFeedbackLabel.SELECTED_ACCEPTED
    assert report.feedback[1].label == rbfsafe.PolicyFeedbackLabel.ELIGIBLE_NOT_SELECTED
    assert report.feedback[2].label == rbfsafe.PolicyFeedbackLabel.POLICY_REJECTED
    assert all(
        item.evidence != rbfsafe.EvidenceLevel.RUNTIME_EXECUTABLE
        for item in report.feedback
    )
    assert gate.telemetry.proposals == 3
    assert gate.telemetry.policy_rejections == 1

    database = rbfsafe.PolicyFeedbackDatabase.create(report.feedback)
    assert database.valid()
    assert database.summary.records == 3
    query = rbfsafe.PolicyFeedbackQuery()
    query.policy_id = "vla-primary"
    assert len(database.query(query)) == 1

    destination = tmp_path / "policy-feedback"
    database.save(destination)
    loaded = rbfsafe.PolicyFeedbackDatabase.load(destination)
    assert [record.id for record in loaded.records] == [record.id for record in report.feedback]

    from rbfsafe.cli import main

    assert main([str(destination), "--policy-id", "vla-primary"]) == 0
    output = capsys.readouterr().out
    assert "RBF-Safe policy-feedback schema=1" in output
    assert "query_records=1" in output
    assert "policy=vla-primary" in output


def test_safety_memory_reuse_fleet_persistence_and_cli(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    def digest(value: str) -> str:
        return value * 64

    def box(lower: float, upper: float) -> rbfsafe.WorkspaceAabb:
        return rbfsafe.WorkspaceAabb([lower, -0.1, -0.1], [upper, 0.1, 0.1])

    def artifact(
        deployment: str, robot: str, scene: str, content: str
    ) -> rbfsafe.MemoryArtifactInput:
        value = rbfsafe.MemoryArtifactInput()
        value.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
        value.deployment_id = deployment
        value.robot_digest = robot
        value.scene_digest = scene
        value.task_id = "shelf-pick"
        value.content_digest = content
        value.locator = f"artifacts/{deployment}"
        value.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
        value.tags = ["production", "shelf"]
        return value

    scene = digest("c")
    robot_a = digest("a")
    robot_b = digest("b")
    memory = rbfsafe.SafetyMemory()
    source_a = memory.register_artifact(artifact("arm-a", robot_a, scene, digest("1")))
    source_b = memory.register_artifact(artifact("arm-b", robot_b, scene, digest("2")))
    assert memory.valid()
    assert memory.summary.active == 2

    query = rbfsafe.MemoryReuseQuery()
    query.deployment_id = "arm-a"
    query.robot_digest = robot_a
    query.scene_digest = scene
    query.target_task_id = "shelf-place"
    query.minimum_evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
    query.required_tags = ["production"]
    candidates = memory.query_reuse(query)
    assert len(candidates) == 1
    assert candidates[0].disposition == rbfsafe.ReuseDisposition.DIRECT
    assert candidates[0].cross_task
    memory.record_reuse(source_a.id, query, "deployment run")
    assert memory.summary.recorded_reuses == 1

    fleet = rbfsafe.make_fleet_snapshot(
        "cell-1",
        scene,
        [
            rbfsafe.FleetMember("arm-a", robot_a, box(-2.0, 2.0)),
            rbfsafe.FleetMember("arm-b", robot_b, box(-2.0, 2.0)),
        ],
    )
    reservation_a = rbfsafe.make_fleet_reservation(
        fleet, memory, "arm-a", source_a.id, box(-1.0, -0.8), 0, 10, 0.05
    )
    reservation_b = rbfsafe.make_fleet_reservation(
        fleet, memory, "arm-b", source_b.id, box(0.8, 1.0), 0, 10, 0.05
    )
    schedule = rbfsafe.analyze_fleet_schedule(fleet, memory, [reservation_b, reservation_a])
    assert (
        schedule.status
        == rbfsafe.FleetScheduleStatus.CONFLICT_FREE_UNDER_DECLARED_ENVELOPES
    )
    assert not schedule.conflicts

    colliding_b = rbfsafe.make_fleet_reservation(
        fleet, memory, "arm-b", source_b.id, box(-0.9, -0.7), 0, 10
    )
    conflicted = rbfsafe.analyze_fleet_schedule(
        fleet, memory, [reservation_a, colliding_b]
    )
    assert conflicted.status == rbfsafe.FleetScheduleStatus.CONFLICTED
    assert conflicted.conflicts[0].reason == rbfsafe.FleetConflictReason.WORKSPACE_OVERLAP

    destination = tmp_path / "safety-memory"
    memory.save(destination)
    loaded = rbfsafe.SafetyMemory.load(destination)
    assert loaded.valid()
    assert loaded.summary.artifacts == 2
    assert loaded.summary.recorded_reuses == 1
    assert loaded.identity == memory.identity

    store_destination = tmp_path / "safety-memory-store"
    store = rbfsafe.SafetyMemoryStore.create(store_destination, memory)
    root_revision = store.current_revision_id
    assert len(store.revisions) == 1
    assert store.revisions[0].memory_id == memory.identity
    memory.transition(
        source_a.id,
        source_a.generation,
        rbfsafe.MemoryArtifactState.STALE,
        "maintenance window",
    )
    revision = store.publish(memory, root_revision)
    assert revision.sequence == 1
    assert revision.parent_id == root_revision
    assert store.load_current().summary.stale == 1
    assert store.load_revision(root_revision).summary.stale == 0
    reopened_store = rbfsafe.SafetyMemoryStore.open(store_destination)
    assert reopened_store.current_revision_id == revision.id
    assert len(reopened_store.revisions) == 2

    from rbfsafe.cli import main

    assert (
        main(
            [
                str(destination),
                "--deployment-id",
                "arm-a",
                "--task-id",
                "shelf-pick",
                "--include-memory-events",
            ]
        )
        == 0
    )
    output = capsys.readouterr().out
    assert "RBF-Safe safety-memory schema=1" in output
    assert "query_artifacts=1" in output
    assert "recorded_reuses=1" in output
    assert "type=safe_atlas state=active deployment=arm-a" in output
    assert "type=reuse_recorded" in output

    assert (
        main(
            [
                str(store_destination),
                "--memory-revision",
                root_revision,
                "--deployment-id",
                "arm-a",
            ]
        )
        == 0
    )
    store_output = capsys.readouterr().out
    assert "RBF-Safe safety-memory-store schema=1" in store_output
    assert "revisions=2" in store_output
    assert f"selected={root_revision}" in store_output
    assert "type=safe_atlas state=active deployment=arm-a" in store_output


def test_tool_link_and_specific_identity_error() -> None:
    robot = rbfsafe.SerialRobotModel(
        "python-tool-1r",
        [rbfsafe.DhJoint(0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE)],
        [rbfsafe.Interval(-1.0, 1.0)],
        [0.02, 0.01],
        rbfsafe.DhJoint(0.0, 0.25, 0.0, 0.0, rbfsafe.JointType.REVOLUTE),
    )
    assert robot.dimension == 1
    assert robot.link_count == 2
    assert len(robot.forward_kinematics([0.0])) == 3

    scene = rbfsafe.SceneSnapshot([], "python-tool-scene")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0]]).atlas
    other_scene = rbfsafe.SceneSnapshot([], "different-version")
    with pytest.raises(rbfsafe.IdentityMismatchError):
        atlas.verify_compatible(robot, other_scene)


def test_trajectory_auditor_and_cli(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    robot = rbfsafe.SerialRobotModel(
        "python-trajectory-prismatic",
        [rbfsafe.DhJoint(0.0, 0.0, 0.0, 0.0, rbfsafe.JointType.PRISMATIC)],
        [rbfsafe.Interval(0.0, 2.0)],
        [0.05],
    )
    scene = rbfsafe.SceneSnapshot(
        [
            rbfsafe.SceneObstacle(
                "high-block",
                rbfsafe.WorkspaceAabb([-0.1, -0.1, 1.1], [0.1, 0.1, 1.2]),
            )
        ],
        "python-trajectory-v1",
    )
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.25]]).atlas
    report = rbfsafe.TrajectoryAuditor().audit(atlas, [[0.25], [1.5]])
    assert report.status == rbfsafe.TrajectoryAuditStatus.PARTIAL
    assert report.coverage_ratio == pytest.approx(0.6)
    assert len(report.region_sequence) == 1
    assert len(report.uncovered_intervals) == 1
    assert report.uncovered_intervals[0].segment_index == 0
    assert report.uncovered_intervals[0].start_fraction == pytest.approx(0.6)
    assert not report.uncovered_intervals[0].start_included
    assert report.uncovered_intervals[0].end_included

    destination = tmp_path / "atlas"
    atlas.save(destination)
    trajectory = tmp_path / "trajectory.json"
    trajectory.write_text(json.dumps({"waypoints": [[0.25], [1.5]]}), encoding="utf-8")
    from rbfsafe.cli import main

    assert main([str(destination), "--trajectory", str(trajectory)]) == 0
    output = capsys.readouterr().out
    assert "trajectory_status=PARTIAL" in output
    assert "trajectory_coverage=0.6" in output
    assert "trajectory_uncovered=0:(0.6,1]" in output
