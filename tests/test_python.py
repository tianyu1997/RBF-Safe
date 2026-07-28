from __future__ import annotations

import json
import hashlib
from pathlib import Path

import pytest
import rbfsafe


def test_version() -> None:
    assert rbfsafe.__version__ == "4.3.0"


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


def test_calibrated_policy_profile_and_gate(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    profile_input = rbfsafe.PolicyCalibrationProfileInput()
    profile_input.policy_id = "vla-primary"
    profile_input.policy_model_digest = "a" * 64
    profile_input.scope_id = "factory-cell-a"
    profile_input.task_id = "shelf-pick"
    profile_input.dataset_digest = "b" * 64
    profile_input.method = "held-out-reliability-bins"
    profile_input.method_version = "1"
    profile_input.outcome_definition = "shield accepted or repaired proposal"
    profile_input.state_uncertainty_unit = "normalized-joint-range-rms"
    profile_input.action_uncertainty_unit = "normalized-joint-range-rms"
    profile_input.bins = [
        rbfsafe.PolicyCalibrationBinInput(0.0, 0.5, 0.25, 500, 100),
        rbfsafe.PolicyCalibrationBinInput(0.5, 1.0, 0.85, 500, 400),
    ]
    profile = rbfsafe.PolicyCalibrationProfile.create(profile_input)
    assert profile.valid()
    assert profile.sample_count == 1_000
    assert profile.lookup(0.5).bin_index == 1
    assert 0.7 < profile.lookup(0.9).conservative_confidence < 0.8

    profile_path = tmp_path / "policy-calibration.json"
    profile.save(profile_path)
    loaded = rbfsafe.PolicyCalibrationProfile.load(profile_path)
    assert loaded.id == profile.id
    from rbfsafe.cli import main

    assert main([str(profile_path), "--policy-confidence", "0.9"]) == 0
    output = capsys.readouterr().out
    assert "RBF-Safe policy-calibration-profile schema=1" in output
    assert "conservative_confidence=" in output
    assert "runtime_executable=false" in output

    def window(
        sequence: int,
        bins: list[rbfsafe.PolicyCalibrationWindowBinInput],
        digest_character: str = "c",
    ) -> rbfsafe.PolicyCalibrationWindowInput:
        value = rbfsafe.PolicyCalibrationWindowInput()
        value.window_id = f"production-window-{sequence}"
        value.sequence = sequence
        value.source_digest = digest_character * 64
        value.bins = bins
        return value

    stable_window = window(
        0,
        [
            rbfsafe.PolicyCalibrationWindowBinInput(500, 100),
            rbfsafe.PolicyCalibrationWindowBinInput(500, 400),
        ],
    )
    drift = rbfsafe.assess_policy_calibration_drift(profile, stable_window)
    assert drift.status == rbfsafe.PolicyCalibrationDriftStatus.STABLE
    assert drift.total_variation_distance == pytest.approx(0.0)
    assert drift.expected_calibration_error == pytest.approx(0.05)
    lifecycle = rbfsafe.PolicyCalibrationLifecycle.create(profile)
    assert lifecycle.state == rbfsafe.PolicyCalibrationLifecycleState.PENDING_REVIEW
    lifecycle.assess(profile, stable_window, lifecycle.current_event_id)
    lifecycle.transition(
        profile,
        lifecycle.current_event_id,
        rbfsafe.PolicyCalibrationLifecycleState.ACTIVE,
        "independent review approved",
    )
    assert lifecycle.valid(profile)
    assert lifecycle.deployment_ready
    lifecycle_path = tmp_path / "policy-calibration-lifecycle.json"
    lifecycle.save(lifecycle_path, profile)
    loaded_lifecycle = rbfsafe.PolicyCalibrationLifecycle.load(lifecycle_path, profile)
    assert loaded_lifecycle.current_event_id == lifecycle.current_event_id
    assert loaded_lifecycle.summary.stable == 1
    assert (
        main(
            [
                str(lifecycle_path),
                "--calibration-profile",
                str(profile_path),
            ]
        )
        == 0
    )
    lifecycle_output = capsys.readouterr().out
    assert "RBF-Safe policy-calibration-lifecycle schema=1" in lifecycle_output
    assert "state=active" in lifecycle_output
    assert "deployment_ready=true" in lifecycle_output
    assert "runtime_executable=false" in lifecycle_output

    robot = make_robot()
    scene = rbfsafe.SceneSnapshot([], "python-calibration-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas

    def calibrated_metadata(sequence: int, confidence: float) -> rbfsafe.PolicyProposalMetadata:
        metadata = rbfsafe.PolicyProposalMetadata()
        metadata.policy_id = "vla-primary"
        metadata.task_id = "shelf-pick"
        metadata.episode_id = "episode-calibration"
        metadata.sequence = sequence
        metadata.confidence = confidence
        metadata.state_uncertainty = 0.05
        metadata.action_uncertainty = 0.04
        return metadata

    proposals = [
        rbfsafe.PolicyProposal(
            rbfsafe.JointDeltaAction([0.05, 0.0]), calibrated_metadata(1, 0.4)
        ),
        rbfsafe.PolicyProposal(
            rbfsafe.JointDeltaAction([0.1, 0.0]), calibrated_metadata(2, 0.9)
        ),
    ]
    options = rbfsafe.CalibratedPolicyGateOptions()
    options.minimum_total_samples = 1_000
    options.minimum_bin_samples = 500
    options.maximum_expected_calibration_error = 0.06
    options.maximum_bin_calibration_error = 0.06
    options.policy.minimum_confidence = 0.7
    options.policy.selection_mode = rbfsafe.PolicySelectionMode.HIGHEST_CONFIDENCE
    gate = rbfsafe.CalibratedPolicySafetyGate()
    report = gate.check_proposals(
        loaded,
        "factory-cell-a",
        "a" * 64,
        robot,
        scene,
        atlas,
        [0.0, 0.0],
        proposals,
        options,
    )
    assert report.profile_id == profile.id
    assert report.applications[0].raw_metadata.confidence == 0.4
    assert report.applications[0].effective_metadata.confidence < 0.4
    assert report.applications[1].effective_metadata.confidence > 0.7
    assert report.policy_report.selected_index == 1
    assert report.policy_report.feedback[0].label == rbfsafe.PolicyFeedbackLabel.POLICY_REJECTED
    assert report.policy_report.feedback[1].evidence != rbfsafe.EvidenceLevel.RUNTIME_EXECUTABLE
    guarded = gate.check_proposals_guarded(
        loaded,
        loaded_lifecycle,
        loaded_lifecycle.current_event_id,
        "factory-cell-a",
        "a" * 64,
        robot,
        scene,
        atlas,
        [0.0, 0.0],
        proposals,
        options,
    )
    assert guarded.lifecycle_event_id == loaded_lifecycle.current_event_id
    drifted_window = window(
        1,
        [
            rbfsafe.PolicyCalibrationWindowBinInput(900, 90),
            rbfsafe.PolicyCalibrationWindowBinInput(100, 60),
        ],
        "d",
    )
    drifted = loaded_lifecycle.assess(
        loaded, drifted_window, loaded_lifecycle.current_event_id
    )
    assert drifted.status == rbfsafe.PolicyCalibrationDriftStatus.DRIFT_DETECTED
    assert loaded_lifecycle.state == rbfsafe.PolicyCalibrationLifecycleState.QUARANTINED
    assert not loaded_lifecycle.deployment_ready
    with pytest.raises(ValueError):
        gate.check_proposals_guarded(
            loaded,
            loaded_lifecycle,
            loaded_lifecycle.current_event_id,
            "factory-cell-a",
            "a" * 64,
            robot,
            scene,
            atlas,
            [0.0, 0.0],
            proposals,
            options,
        )


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

    archive = rbfsafe.FleetScheduleArchive.create("cell-1")
    root_schedule = archive.publish(fleet, memory, [reservation_b, reservation_a], "")
    assert root_schedule.sequence == 0
    assert root_schedule.memory_id == memory.identity
    assert root_schedule.report.id == schedule.id
    assert archive.valid()
    assert (
        archive.publish(
            fleet,
            memory,
            [reservation_b, reservation_a],
            root_schedule.id,
        ).id
        == root_schedule.id
    )
    current_schedule = archive.publish(
        fleet,
        memory,
        [reservation_a, colliding_b],
        root_schedule.id,
    )
    assert current_schedule.sequence == 1
    assert current_schedule.parent_id == root_schedule.id
    assert archive.verify_version(root_schedule.id, fleet, memory).id == schedule.id
    archive_destination = tmp_path / "fleet-schedule-archive"
    archive.save(archive_destination)
    loaded_archive = rbfsafe.FleetScheduleArchive.load(archive_destination)
    assert loaded_archive.valid()
    assert loaded_archive.fleet_id == "cell-1"
    assert loaded_archive.current_version_id == current_schedule.id
    assert loaded_archive.current_version().report.status == rbfsafe.FleetScheduleStatus.CONFLICTED

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
    with pytest.raises(rbfsafe.IdentityMismatchError):
        archive.verify_version(current_schedule.id, fleet, memory)

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

    assert main([str(archive_destination)]) == 0
    archive_output = capsys.readouterr().out
    assert "RBF-Safe fleet-schedule-archive schema=1" in archive_output
    assert "fleet=cell-1 versions=2" in archive_output
    assert f"current={current_schedule.id}" in archive_output
    assert "status=conflicted reservations=2 conflicts=1 pair_evaluations=1" in archive_output

    assert main([str(archive_destination), "--fleet-schedule-version", root_schedule.id]) == 0
    root_output = capsys.readouterr().out
    assert f"selected={root_schedule.id} sequence=0 parent=" in root_output
    assert "status=conflict_free_under_declared_envelopes" in root_output


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


def test_authenticated_artifact_attestation(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    def digest(value: str) -> str:
        return value * 64

    item = rbfsafe.MemoryArtifactInput()
    item.type = rbfsafe.MemoryArtifactType.SAFE_ATLAS
    item.deployment_id = "arm-a"
    item.robot_digest = digest("a")
    item.scene_digest = digest("b")
    item.task_id = "shelf-pick"
    item.content_digest = digest("c")
    item.locator = "artifacts/shelf-atlas"
    item.evidence = rbfsafe.EvidenceLevel.CERTIFIED_REGION
    memory = rbfsafe.SafetyMemory()
    artifact = memory.register_artifact(item)

    payload = b"immutable atlas payload\n"
    key = bytes(range(1, 33))
    attestation = rbfsafe.attest_artifact(
        artifact,
        payload,
        "factory-service",
        "rotation-7",
        key,
        12,
        "application/vnd.rbfsafe.atlas",
    )
    assert rbfsafe.valid_artifact_attestation(attestation)
    assert attestation.artifact_id == artifact.id
    assert attestation.payload_bytes == len(payload)
    assert attestation.algorithm == rbfsafe.ArtifactAuthenticationAlgorithm.HMAC_SHA256
    assert rbfsafe.verify_artifact(
        artifact, payload, attestation, "factory-service", "rotation-7", key
    ) is None
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.verify_artifact(
            artifact,
            payload,
            attestation,
            "factory-service",
            "rotation-7",
            b"x" * 32,
        )

    payload_path = tmp_path / "atlas.bin"
    payload_path.write_bytes(payload)
    file_attestation = rbfsafe.attest_artifact_file(
        artifact,
        payload_path,
        "factory-service",
        "rotation-7",
        key,
        13,
        "application/vnd.rbfsafe.atlas",
    )
    rbfsafe.verify_artifact_file(
        artifact,
        payload_path,
        file_attestation,
        "factory-service",
        "rotation-7",
        key,
    )
    attestation_path = tmp_path / "atlas.attestation.json"
    rbfsafe.save_artifact_attestation(file_attestation, attestation_path)
    loaded = rbfsafe.load_artifact_attestation(attestation_path)
    assert loaded.id == file_attestation.id
    assert rbfsafe.artifact_authentication_algorithm_name(loaded.algorithm) == "hmac_sha256"
    limited = rbfsafe.ArtifactVerificationOptions()
    limited.maximum_payload_bytes = 1
    with pytest.raises(MemoryError):
        rbfsafe.verify_artifact_file(
            artifact,
            payload_path,
            loaded,
            "factory-service",
            "rotation-7",
            key,
            limited,
        )

    memory_path = tmp_path / "safety-memory"
    memory.save(memory_path)
    key_path = tmp_path / "attestation.key"
    key_path.write_bytes(key)
    from rbfsafe.cli import main

    assert main([str(attestation_path)]) == 0
    inspect_output = capsys.readouterr().out
    assert "RBF-Safe artifact-attestation schema=1" in inspect_output
    assert "verified=false" in inspect_output
    assert (
        main(
            [
                str(attestation_path),
                "--artifact-payload",
                str(payload_path),
                "--attestation-memory",
                str(memory_path),
                "--hmac-key-file",
                str(key_path),
                "--expected-service-id",
                "factory-service",
                "--expected-key-id",
                "rotation-7",
            ]
        )
        == 0
    )
    assert "verified=true" in capsys.readouterr().out


def test_remote_artifact_transfer_and_journal(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
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
    key = bytes(range(1, 33))

    publish = rbfsafe.prepare_artifact_publish(
        memory,
        artifact.id,
        payload,
        "artifact-service",
        10,
        "application/vnd.rbfsafe.atlas",
    )
    receipt = rbfsafe.make_artifact_publish_receipt(publish, 101)
    receipt = rbfsafe.authenticate_artifact_publish_receipt(
        receipt, "service-key-1", key
    )
    verified_publish = rbfsafe.verify_artifact_publish(
        memory, publish, receipt, payload, "service-key-1", key
    )
    assert verified_publish.operation == rbfsafe.ArtifactTransferOperation.PUBLISH
    assert (
        verified_publish.authentication
        == rbfsafe.ArtifactTransferAuthentication.HMAC_SHA256
    )

    fetch = rbfsafe.prepare_artifact_fetch(
        memory,
        artifact.id,
        "artifact-service",
        11,
        "application/vnd.rbfsafe.atlas",
    )
    response = rbfsafe.make_artifact_fetch_response(fetch, payload, 102)
    response = rbfsafe.authenticate_artifact_fetch_response(
        response, "service-key-1", key
    )
    verified_fetch = rbfsafe.verify_artifact_fetch(
        memory, fetch, response, payload, "service-key-1", key
    )
    assert verified_fetch.operation == rbfsafe.ArtifactTransferOperation.FETCH
    assert rbfsafe.valid_verified_artifact_transfer(verified_fetch)

    journal = rbfsafe.ArtifactTransferJournal()
    first = journal.append(verified_publish, "")
    second = journal.append(verified_fetch, journal.current_record_id)
    assert first.sequence == 1
    assert second.sequence == 2
    assert second.parent_id == first.id
    destination = tmp_path / "transfer-journal"
    journal.save(destination)
    loaded = rbfsafe.ArtifactTransferJournal.load(destination)
    assert loaded.identity == journal.identity
    assert len(loaded.records) == 2
    assert (
        rbfsafe.artifact_transfer_operation_name(
            loaded.records[-1].transfer.operation
        )
        == "fetch"
    )
    from rbfsafe.cli import main

    assert main([str(destination)]) == 0
    output = capsys.readouterr().out
    assert "RBF-Safe artifact-transfer-journal schema=2" in output
    assert "records=2" in output
    assert "operation=fetch" in output
    assert "authentication=hmac_sha256" in output
    assert "runtime_executable=false" in output

    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.verify_artifact_fetch(
            memory, fetch, response, b"altered", "service-key-1", key
        )


def test_public_service_identity_and_offline_verification(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
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
    assert rbfsafe.valid_service_public_key(service_key)
    assert service_key.allow_rotate
    bundle = rbfsafe.ServiceTrustBundle.create(1, "", [service_key])
    assert bundle.storage_schema == 2

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
    inconsistent_secret = bytearray(key_pair.secret_key)
    inconsistent_secret[0] ^= 1
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.sign_artifact_publish_receipt(
            receipt, service_key.id, bytes(inconsistent_secret)
        )
    receipt = rbfsafe.sign_artifact_publish_receipt(
        receipt, service_key.id, key_pair.secret_key
    )
    verified = rbfsafe.verify_artifact_publish_offline(
        memory, request, receipt, payload, bundle
    )
    assert verified.authentication == rbfsafe.ArtifactTransferAuthentication.ED25519
    assert verified.verification_key_id == service_key.id
    assert verified.trust_bundle_id == bundle.id
    assert (
        rbfsafe.artifact_authentication_algorithm_name(
            receipt.service_attestation.algorithm
        )
        == "ed25519"
    )

    pending_key = rbfsafe.make_service_public_key(
        "artifact-service",
        key_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.PENDING,
    )
    pending_bundle = rbfsafe.ServiceTrustBundle.create(1, "", [pending_key])
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.verify_artifact_publish_offline(
            memory, request, receipt, payload, pending_bundle
        )

    bundle_path = tmp_path / "service-trust-bundle.json"
    bundle.save(bundle_path)
    loaded_bundle = rbfsafe.ServiceTrustBundle.load(bundle_path)
    assert loaded_bundle.id == bundle.id
    assert loaded_bundle.key("artifact-service", service_key.id).id == service_key.id

    successor_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
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
        bundle, [retired_key, successor_key]
    )
    authorization = rbfsafe.authorize_service_trust_bundle_successor(
        bundle,
        successor,
        service_key.service_id,
        service_key.id,
        key_pair.secret_key,
    )
    assert rbfsafe.valid_service_trust_bundle_authorization(authorization)
    rbfsafe.verify_service_trust_bundle_successor(bundle, successor, authorization)
    history_path = tmp_path / "service-trust-history"
    history = rbfsafe.ServiceTrustHistory.create(history_path, bundle, bundle.id)
    record = history.publish(successor, authorization, bundle.id)
    assert (
        record.type
        == rbfsafe.ServiceTrustRotationEventType.SUCCESSOR_AUTHORIZED
    )
    assert history.current_bundle_id == successor.id
    loaded_history = rbfsafe.ServiceTrustHistory.open(
        history_path, bundle.id, successor.id
    )
    assert loaded_history.valid()
    assert len(loaded_history.records) == 2
    assert loaded_history.current_bundle().id == successor.id
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.ServiceTrustHistory.open(history_path, bundle.id, bundle.id)

    journal = rbfsafe.ArtifactTransferJournal()
    journal.append(verified, "")
    journal_path = tmp_path / "public-transfer-journal"
    journal.save(journal_path)
    loaded_journal = rbfsafe.ArtifactTransferJournal.load(journal_path)
    assert loaded_journal.records[0].transfer.trust_bundle_id == bundle.id

    data_root = Path(__file__).resolve().parents[1] / "data"
    fixed_bundle = rbfsafe.ServiceTrustBundle.load(
        data_root / "service_trust_bundle_schema1" / "bundle.json"
    )
    assert (
        fixed_bundle.id
        == "b6f6e30bc2245e64a519c8a02e61063bdf2fe1d8dc5ee35d980f46e1e4aa584d"
    )
    assert fixed_bundle.storage_schema == 1
    assert not fixed_bundle.keys[0].allow_rotate
    fixed_schema2_bundle = rbfsafe.ServiceTrustBundle.load(
        data_root / "service_trust_bundle_schema2" / "bundle.json"
    )
    assert (
        fixed_schema2_bundle.id
        == "9e30c7b54a023db15fdb3751592e4291f6714f1751183860e1013d486c0357bd"
    )
    fixed_history = rbfsafe.ServiceTrustHistory.open(
        data_root / "service_trust_history_schema1",
        "9e30c7b54a023db15fdb3751592e4291f6714f1751183860e1013d486c0357bd",
        "4c119f290036039ce28f4c8b8d8db572a7950cdf28e907153ef4c02445afad3b",
    )
    assert len(fixed_history.records) == 2
    assert (
        fixed_history.records[1].authorization.id
        == "68debbfc4156e5c829d641e5b9ed4aeab04174454d655d5558783f81fc8711d3"
    )
    fixed_schema3_bundle = rbfsafe.ServiceTrustBundle.load(
        data_root / "service_trust_bundle_schema3" / "bundle.json"
    )
    assert fixed_schema3_bundle.storage_schema == 3
    assert (
        fixed_schema3_bundle.id
        == "e9126145264ac126845b17db5db782a43a4816d5d4ca3a9d4f9462d874e7b89b"
    )
    assert fixed_schema3_bundle.rotation_policy.minimum_signatures == 2
    assert fixed_schema3_bundle.rotation_policy.require_distinct_services
    fixed_checkpoint = rbfsafe.ServiceTrustCheckpoint.load(
        data_root / "service_trust_checkpoint_schema1" / "checkpoint.json"
    )
    assert (
        fixed_checkpoint.id
        == "9cbf2bfad11354201ec0eb79dd1f11cf78925e4ea917cc3a7e5d15f6307a2e24"
    )
    fixed_quorum_history = rbfsafe.ServiceTrustHistory.open(
        data_root / "service_trust_history_schema2",
        fixed_schema3_bundle.id,
        fixed_checkpoint,
        fixed_checkpoint.id,
    )
    assert fixed_quorum_history.storage_schema == 2
    assert (
        fixed_quorum_history.current_bundle_id
        == "d31c074a89a038167c34b1a65934186c0696aff196165dbdac67d0839cd34fb6"
    )
    assert (
        fixed_quorum_history.records[1].authorization_set.id
        == "ee8d89a646c7b3acb4d94c7f321ad6dcdfc96a2347a62ff1d0c17e8b4df61870"
    )
    fixed_journal = rbfsafe.ArtifactTransferJournal.load(
        data_root / "artifact_transfer_journal_schema2"
    )
    assert fixed_journal.records[0].transfer.trust_bundle_id == fixed_bundle.id

    from rbfsafe.cli import main

    assert main([str(bundle_path)]) == 0
    bundle_output = capsys.readouterr().out
    assert "RBF-Safe service-trust-bundle schema=2" in bundle_output
    assert "state=active" in bundle_output
    assert "algorithm=ed25519" in bundle_output
    assert "caller_pinned=false" in bundle_output
    assert "rotate=true" in bundle_output
    assert (
        main(
            [
                str(history_path),
                "--expected-trust-root",
                bundle.id,
                "--expected-trust-head",
                successor.id,
            ]
        )
        == 0
    )
    history_output = capsys.readouterr().out
    assert "RBF-Safe service-trust-history schema=1" in history_output
    assert "records=2" in history_output
    assert f"root={bundle.id}" in history_output
    assert f"head={successor.id}" in history_output
    assert "type=successor_authorized" in history_output
    with pytest.raises(SystemExit):
        main([str(history_path)])
    with pytest.raises(rbfsafe.IdentityMismatchError):
        main(
            [
                str(history_path),
                "--expected-trust-root",
                bundle.id,
                "--expected-trust-head",
                bundle.id,
            ]
        )
    assert (
        main(
            [
                str(data_root / "service_trust_history_schema1"),
                "--expected-trust-root",
                fixed_history.root_bundle_id,
                "--expected-trust-head",
                fixed_history.current_bundle_id,
            ]
        )
        == 0
    )
    assert "records=2" in capsys.readouterr().out
    assert main([str(journal_path)]) == 0
    journal_output = capsys.readouterr().out
    assert "RBF-Safe artifact-transfer-journal schema=2" in journal_output
    assert f"trust_bundle={bundle.id}" in journal_output


def test_quorum_rotation_and_signed_checkpoint(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    pair_a = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    pair_b = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
    key_a = rbfsafe.make_service_public_key(
        "rotation-a",
        pair_a.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        False,
        True,
    )
    key_b = rbfsafe.make_service_public_key(
        "rotation-b",
        pair_b.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        False,
        True,
    )
    policy = rbfsafe.ServiceTrustRotationPolicy()
    policy.minimum_signatures = 2
    policy.require_distinct_services = True
    assert rbfsafe.valid_service_trust_rotation_policy(policy)
    root = rbfsafe.ServiceTrustBundle.create_with_rotation_policy(
        1, "", [key_b, key_a], policy
    )
    assert root.storage_schema == 3
    assert root.rotation_policy.minimum_signatures == 2
    successor = rbfsafe.rotate_service_trust_bundle(root, list(root.keys))
    authorization_a = rbfsafe.authorize_service_trust_bundle_successor(
        root, successor, key_a.service_id, key_a.id, pair_a.secret_key
    )
    authorization_b = rbfsafe.authorize_service_trust_bundle_successor(
        root, successor, key_b.service_id, key_b.id, pair_b.secret_key
    )
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.verify_service_trust_bundle_successor(
            root, successor, authorization_a
        )
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.assemble_service_trust_bundle_authorizations(
            root, successor, [authorization_a]
        )
    authorization_set = rbfsafe.assemble_service_trust_bundle_authorizations(
        root, successor, [authorization_b, authorization_a]
    )
    assert rbfsafe.valid_service_trust_bundle_authorization_set(authorization_set)
    assert authorization_set.authorizations[0].signer_service_id == "rotation-a"
    rbfsafe.verify_service_trust_bundle_successor(
        root, successor, authorization_set
    )

    history_path = tmp_path / "quorum-history"
    history = rbfsafe.ServiceTrustHistory.create(history_path, root, root.id)
    assert history.storage_schema == 2
    record = history.publish(successor, authorization_set, root.id)
    assert record.storage_schema == 2
    assert record.authorization is None
    assert len(record.authorization_set.authorizations) == 2

    signature_a = rbfsafe.sign_service_trust_checkpoint(
        history, key_a.service_id, key_a.id, pair_a.secret_key
    )
    signature_b = rbfsafe.sign_service_trust_checkpoint(
        history, key_b.service_id, key_b.id, pair_b.secret_key
    )
    assert rbfsafe.valid_service_trust_checkpoint_signature(signature_a)
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.assemble_service_trust_checkpoint(history, [signature_a])
    checkpoint = rbfsafe.assemble_service_trust_checkpoint(
        history, [signature_b, signature_a]
    )
    assert checkpoint.valid()
    assert checkpoint.signatures[0].signer_service_id == "rotation-a"
    rbfsafe.verify_service_trust_checkpoint(history, checkpoint, checkpoint.id)

    checkpoint_path = tmp_path / "trust-checkpoint.json"
    checkpoint.save(checkpoint_path)
    loaded = rbfsafe.ServiceTrustCheckpoint.load(checkpoint_path)
    assert loaded.id == checkpoint.id
    reopened = rbfsafe.ServiceTrustHistory.open(
        history_path, root.id, loaded, loaded.id
    )
    assert reopened.current_bundle_id == successor.id
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.ServiceTrustHistory.open(
            history_path, root.id, loaded, "f" * 64
        )
    from rbfsafe.cli import main

    assert (
        main(
            [
                str(history_path),
                "--expected-trust-root",
                root.id,
                "--trust-checkpoint",
                str(checkpoint_path),
                "--expected-trust-checkpoint",
                checkpoint.id,
            ]
        )
        == 0
    )
    history_output = capsys.readouterr().out
    assert "service-trust-history schema=2" in history_output
    assert "authorization_set=" in history_output
    assert "checkpoint_verified=true" in history_output
    assert (
        main(
            [
                str(checkpoint_path),
                "--expected-trust-root",
                root.id,
                "--trust-history",
                str(history_path),
                "--expected-trust-checkpoint",
                checkpoint.id,
            ]
        )
        == 0
    )
    checkpoint_output = capsys.readouterr().out
    assert "service-trust-checkpoint schema=1" in checkpoint_output
    assert "signatures=2" in checkpoint_output
    assert "checkpoint_verified=true" in checkpoint_output


def test_reviewed_deployment_profile(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    pair_a = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    pair_b = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
    governance_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(65, 97)))
    reviewer_a = rbfsafe.make_service_public_key(
        "review-safety",
        pair_a.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    reviewer_b = rbfsafe.make_service_public_key(
        "review-controls",
        pair_b.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    governance = rbfsafe.make_service_public_key(
        "trust-governance",
        governance_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        False,
        True,
    )
    rotation_policy = rbfsafe.ServiceTrustRotationPolicy()
    rotation_policy.minimum_signatures = 1
    bundle = rbfsafe.ServiceTrustBundle.create_with_rotation_policy(
        1, "", [reviewer_b, governance, reviewer_a], rotation_policy
    )
    history_path = tmp_path / "deployment-trust-history"
    history = rbfsafe.ServiceTrustHistory.create(history_path, bundle, bundle.id)
    checkpoint_signature = rbfsafe.sign_service_trust_checkpoint(
        history,
        governance.service_id,
        governance.id,
        governance_pair.secret_key,
    )
    checkpoint = rbfsafe.assemble_service_trust_checkpoint(
        history, [checkpoint_signature]
    )
    checkpoint_path = tmp_path / "deployment-trust-checkpoint.json"
    checkpoint.save(checkpoint_path)

    constraints = rbfsafe.DeploymentRuntimeConstraints()
    constraints.maximum_observation_age_ns = 50_000
    constraints.maximum_command_latency_ns = 50_000
    constraints.maximum_control_period_ns = 2_000_000
    constraints.maximum_consecutive_missed_cycles = 1
    policy = rbfsafe.DeploymentReviewPolicy()
    policy.minimum_approvals = 2
    policy.require_distinct_services = True
    policy.required_roles = [
        rbfsafe.DeploymentReviewRole.CONTROLS,
        rbfsafe.DeploymentReviewRole.SAFETY,
    ]
    profile_input = rbfsafe.DeploymentProfileInput()
    profile_input.deployment_id = "cell-a"
    profile_input.robot_digest = "a" * 64
    profile_input.controller_digest = "b" * 64
    profile_input.platform_digest = "c" * 64
    profile_input.runtime_digest = "d" * 64
    profile_input.trust_root_bundle_id = bundle.id
    profile_input.trust_checkpoint_id = checkpoint.id
    profile_input.trust_bundle_id = bundle.id
    profile_input.trust_bundle_sequence = bundle.sequence
    profile_input.runtime_constraints = constraints
    profile_input.review_policy = policy
    profile = rbfsafe.DeploymentProfile.create(profile_input)
    assert profile.valid()
    assert profile.review_policy.required_roles[0] == rbfsafe.DeploymentReviewRole.SAFETY

    approval_a = rbfsafe.sign_deployment_profile_approval(
        profile,
        reviewer_a.service_id,
        reviewer_a.id,
        rbfsafe.DeploymentReviewRole.SAFETY,
        pair_a.secret_key,
    )
    approval_b = rbfsafe.sign_deployment_profile_approval(
        profile,
        reviewer_b.service_id,
        reviewer_b.id,
        rbfsafe.DeploymentReviewRole.CONTROLS,
        pair_b.secret_key,
    )
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.assemble_deployment_profile_approvals(profile, [approval_a])
    approval_set = rbfsafe.assemble_deployment_profile_approvals(
        profile, [approval_b, approval_a]
    )
    rbfsafe.verify_deployment_profile_approvals(profile, approval_set, bundle)
    reviewed = rbfsafe.ReviewedDeploymentProfile.create(
        profile, approval_set, history, checkpoint, checkpoint.id
    )
    assert reviewed.valid()
    assert not reviewed.authorizes_execution

    snapshot = rbfsafe.DeploymentRuntimeSnapshot()
    snapshot.deployment_id = "cell-a"
    snapshot.robot_digest = "a" * 64
    snapshot.controller_digest = "b" * 64
    snapshot.platform_digest = "c" * 64
    snapshot.runtime_digest = "d" * 64
    snapshot.observation_age_ns = 10_000
    snapshot.command_latency_ns = 20_000
    snapshot.control_period_ns = 1_000_000
    snapshot.runtime_monitor_active = True
    snapshot.fail_closed_transport_active = True
    snapshot.authenticated_artifacts = True
    assessment = reviewed.assess(snapshot)
    assert assessment.status == rbfsafe.DeploymentProfileAssessmentStatus.CONFORMANT
    assert assessment.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not assessment.authorizes_execution
    snapshot.runtime_monitor_active = False
    failed = reviewed.assess(snapshot)
    assert failed.status == rbfsafe.DeploymentProfileAssessmentStatus.NONCONFORMANT
    assert failed.violations == [
        rbfsafe.DeploymentConstraintViolation.RUNTIME_MONITOR_REQUIRED
    ]

    profile_path = tmp_path / "reviewed-deployment-profile.json"
    reviewed.save(profile_path)
    loaded = rbfsafe.ReviewedDeploymentProfile.load(
        profile_path, history, checkpoint, checkpoint.id
    )
    assert loaded.profile.id == profile.id
    assert loaded.approval_set.id == approval_set.id

    fixture_root = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "reviewed_deployment_profile_schema1"
    )
    fixed_checkpoint = rbfsafe.ServiceTrustCheckpoint.load(
        fixture_root / "checkpoint.json"
    )
    fixed_history = rbfsafe.ServiceTrustHistory.open(
        fixture_root / "trust-history",
        fixed_checkpoint.root_bundle_id,
        fixed_checkpoint,
        fixed_checkpoint.id,
    )
    fixed_reviewed = rbfsafe.ReviewedDeploymentProfile.load(
        fixture_root / "profile.json",
        fixed_history,
        fixed_checkpoint,
        fixed_checkpoint.id,
    )
    assert (
        fixed_reviewed.profile.id
        == "c652aa75ca153ef429b6fff372b83c675a0ac3b68f055e9bb607108d543c7be4"
    )
    assert (
        fixed_reviewed.approval_set.id
        == "e56a57547437938c39ca903541cf7eb014d921a3254fc06e8a45c96e6d8cd9ae"
    )

    from rbfsafe.cli import main

    assert (
        main(
            [
                str(profile_path),
                "--expected-trust-root",
                bundle.id,
                "--trust-history",
                str(history_path),
                "--trust-checkpoint",
                str(checkpoint_path),
                "--expected-trust-checkpoint",
                checkpoint.id,
            ]
        )
        == 0
    )
    output = capsys.readouterr().out
    assert "reviewed-deployment-profile schema=1" in output
    assert "approvals=2" in output
    assert "review_signatures_verified=true" in output
    assert "runtime_executable=false" in output


def test_bounded_execution_session(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    robot = rbfsafe.SerialRobotModel(
        "python-execution-planar",
        [
            rbfsafe.DhJoint(
                0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE
            ),
            rbfsafe.DhJoint(
                0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE
            ),
        ],
        [rbfsafe.Interval(-1.5, 1.5), rbfsafe.Interval(-1.5, 1.5)],
        [0.05, 0.05],
    )
    scene = rbfsafe.SceneSnapshot([], "python-execution-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas

    safety_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    controls_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
    governance_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(65, 97)))
    controller_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(97, 129)))
    monitor_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(129, 161)))
    safety_key = rbfsafe.make_service_public_key(
        "python-execution-safety",
        safety_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    controls_key = rbfsafe.make_service_public_key(
        "python-execution-controls",
        controls_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    governance_key = rbfsafe.make_service_public_key(
        "python-execution-governance",
        governance_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        False,
        True,
    )
    rotation = rbfsafe.ServiceTrustRotationPolicy()
    bundle = rbfsafe.ServiceTrustBundle.create_with_rotation_policy(
        1, "", [controls_key, governance_key, safety_key], rotation
    )
    history_path = tmp_path / "execution-trust-history"
    history = rbfsafe.ServiceTrustHistory.create(
        history_path, bundle, bundle.id
    )
    checkpoint_signature = rbfsafe.sign_service_trust_checkpoint(
        history,
        governance_key.service_id,
        governance_key.id,
        governance_pair.secret_key,
    )
    checkpoint = rbfsafe.assemble_service_trust_checkpoint(
        history, [checkpoint_signature]
    )
    checkpoint_path = tmp_path / "execution-checkpoint.json"
    checkpoint.save(checkpoint_path)

    constraints = rbfsafe.DeploymentRuntimeConstraints()
    constraints.maximum_observation_age_ns = 1_000_000
    constraints.maximum_command_latency_ns = 20_000
    constraints.maximum_control_period_ns = 50_000
    policy = rbfsafe.DeploymentReviewPolicy()
    policy.minimum_approvals = 2
    policy.require_distinct_services = True
    policy.required_roles = [
        rbfsafe.DeploymentReviewRole.SAFETY,
        rbfsafe.DeploymentReviewRole.CONTROLS,
    ]
    profile_input = rbfsafe.DeploymentProfileInput()
    profile_input.deployment_id = "python-execution-cell"
    profile_input.robot_digest = atlas.robot_digest
    profile_input.controller_digest = "b" * 64
    profile_input.platform_digest = "c" * 64
    profile_input.runtime_digest = "d" * 64
    profile_input.trust_root_bundle_id = bundle.id
    profile_input.trust_checkpoint_id = checkpoint.id
    profile_input.trust_bundle_id = bundle.id
    profile_input.trust_bundle_sequence = bundle.sequence
    profile_input.runtime_constraints = constraints
    profile_input.review_policy = policy
    profile = rbfsafe.DeploymentProfile.create(profile_input)
    profile_safety = rbfsafe.sign_deployment_profile_approval(
        profile,
        safety_key.service_id,
        safety_key.id,
        rbfsafe.DeploymentReviewRole.SAFETY,
        safety_pair.secret_key,
    )
    profile_controls = rbfsafe.sign_deployment_profile_approval(
        profile,
        controls_key.service_id,
        controls_key.id,
        rbfsafe.DeploymentReviewRole.CONTROLS,
        controls_pair.secret_key,
    )
    profile_approvals = rbfsafe.assemble_deployment_profile_approvals(
        profile, [profile_controls, profile_safety]
    )
    reviewed = rbfsafe.ReviewedDeploymentProfile.create(
        profile, profile_approvals, history, checkpoint, checkpoint.id
    )
    reviewed_path = tmp_path / "reviewed-profile.json"
    reviewed.save(reviewed_path)

    configurations = [[-1.0, -1.0], [0.0, 0.0], [1.0, 1.0]]
    sequence = rbfsafe.ExecutionCommandSequence.create(
        atlas, configurations, [0, 50_000, 100_000]
    )
    assert sequence.valid()
    sequence.verify_compatible(atlas)
    controller = rbfsafe.make_execution_endpoint_key(
        "python-execution-controller",
        rbfsafe.ExecutionEndpointRole.CONTROLLER,
        controller_pair.public_key,
    )
    monitor = rbfsafe.make_execution_endpoint_key(
        "python-execution-monitor",
        rbfsafe.ExecutionEndpointRole.RUNTIME_MONITOR,
        monitor_pair.public_key,
    )
    limits = rbfsafe.ExecutionSessionLimits()
    limits.maximum_start_delay_ns = 10_000
    limits.maximum_duration_ns = 100_000
    limits.maximum_commands = 3
    request_input = rbfsafe.ExecutionSessionRequestInput()
    request_input.session_nonce = "9" * 64
    request_input.controller = controller
    request_input.runtime_monitor = monitor
    request_input.limits = limits
    request = rbfsafe.ExecutionSessionRequest.create(
        reviewed, sequence, request_input
    )
    session_safety = rbfsafe.sign_execution_session_approval(
        request, profile_safety, safety_pair.secret_key
    )
    session_controls = rbfsafe.sign_execution_session_approval(
        request, profile_controls, controls_pair.secret_key
    )
    session_approvals = rbfsafe.assemble_execution_session_approvals(
        request, reviewed, [session_safety, session_controls]
    )
    rbfsafe.verify_execution_session_approvals(
        request, reviewed, session_approvals, bundle
    )
    controller_ack = rbfsafe.sign_execution_controller_acknowledgement(
        request, controller_pair.secret_key
    )

    runtime = rbfsafe.DeploymentRuntimeSnapshot()
    runtime.deployment_id = profile.deployment_id
    runtime.robot_digest = atlas.robot_digest
    runtime.controller_digest = profile.controller_digest
    runtime.platform_digest = profile.platform_digest
    runtime.runtime_digest = profile.runtime_digest
    runtime.observation_age_ns = 10_000
    runtime.command_latency_ns = 20_000
    runtime.control_period_ns = 50_000
    runtime.runtime_monitor_active = True
    runtime.fail_closed_transport_active = True
    runtime.authenticated_artifacts = True
    observation_input = rbfsafe.ExecutionRuntimeObservationInput()
    observation_input.runtime = runtime
    observation_input.observation_sequence = 7
    observation_input.observed_monotonic_ns = 1_000_000
    observation_input.monitor_state = (
        rbfsafe.ExecutionMonitorState.ARMED_CERTIFIED_SEQUENCE
    )
    observation = rbfsafe.ExecutionRuntimeObservation.create(
        request, observation_input
    )
    monitor_ack = rbfsafe.sign_execution_monitor_acknowledgement(
        request, observation, monitor_pair.secret_key
    )
    session = rbfsafe.BoundedExecutionSession.create(
        request,
        sequence,
        session_approvals,
        controller_ack,
        monitor_ack,
        reviewed,
        bundle,
        atlas,
    )
    assert session.valid()
    assert session.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not session.authorizes_execution
    exact = session.authorize_command(1, configurations[1], 1_050_001)
    assert exact is not None
    assert exact.valid()
    assert exact.evidence == rbfsafe.EvidenceLevel.RUNTIME_EXECUTABLE
    assert not exact.open_ended
    assert session.authorize_command(1, configurations[0], 1_050_001) is None
    assert session.authorize_command(1, configurations[1], 1_049_999) is None

    atlas_path = tmp_path / "execution-atlas"
    atlas.save(atlas_path)
    session_path = tmp_path / "execution-session.json"
    session.save(session_path)
    loaded = rbfsafe.BoundedExecutionSession.load(
        session_path,
        reviewed,
        history,
        checkpoint,
        checkpoint.id,
        atlas,
    )
    assert loaded.id == session.id

    ledger_path = tmp_path / "execution-ledger"
    ledger = rbfsafe.ExecutionLedger.create(ledger_path, session)
    dispatch_times = (1_000_000, 1_050_001, 1_100_000)
    completion_times = (1_000_005, 1_050_005, 1_100_000)
    for index, configuration in enumerate(configurations):
        decision = ledger.authorize_command(
            session,
            reviewed,
            history,
            checkpoint,
            checkpoint.id,
            atlas,
            index,
            configuration,
            dispatch_times[index],
            ledger.current_record_id,
        )
        assert decision.valid()
        assert decision.authorization is not None
        assert decision.authorizes_execution
        assert decision.evidence == rbfsafe.EvidenceLevel.RUNTIME_EXECUTABLE
        completion_input = rbfsafe.ExecutionControllerCompletionInput()
        completion_input.outcome = rbfsafe.ExecutionCompletionOutcome.COMPLETED
        completion_input.completed_monotonic_ns = completion_times[index]
        completion_input.result_digest = str(index + 1) * 64
        completion = rbfsafe.sign_execution_controller_completion(
            session,
            decision.authorization,
            completion_input,
            controller_pair.secret_key,
        )
        assert completion.valid()
        rbfsafe.verify_execution_controller_completion(
            session, decision.authorization, completion
        )
        ledger.record_completion(
            session,
            reviewed,
            history,
            atlas,
            completion,
            ledger.current_record_id,
        )
    assert ledger.summary.status == rbfsafe.ExecutionLedgerStatus.COMPLETED
    assert not ledger.summary.authorizes_execution
    ledger_audit = ledger.audit(session, reviewed, history, atlas)
    assert ledger_audit.valid()
    assert ledger_audit.verified_records == 7
    assert ledger_audit.verified_checkpoints == 3
    assert ledger_audit.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not ledger_audit.authorizes_execution

    from rbfsafe.cli import main

    common = [
        str(session_path),
        "--reviewed-profile",
        str(reviewed_path),
        "--execution-atlas",
        str(atlas_path),
        "--trust-history",
        str(history_path),
        "--trust-checkpoint",
        str(checkpoint_path),
        "--expected-trust-root",
        bundle.id,
        "--expected-trust-checkpoint",
        checkpoint.id,
    ]
    assert main(common) == 0
    output = capsys.readouterr().out
    assert "bounded-execution-session schema=1 commands=3 approvals=2" in output
    assert "session_evidence=unknown" in output
    assert "session_authorizes_execution=false" in output
    assert "command_authorization_requires_exact_runtime_input=true" in output
    assert (
        main(
            common
            + [
                "--execution-command-index",
                "1",
                "--execution-configuration",
                "0",
                "0",
                "--dispatch-monotonic-ns",
                "1050001",
            ]
        )
        == 0
    )
    exact_output = capsys.readouterr().out
    assert "command_authorized=true" in exact_output
    assert "command_evidence=runtime_executable" in exact_output
    assert "command_open_ended=false" in exact_output
    assert (
        main(
            [
                str(ledger_path),
                "--execution-session",
                str(session_path),
                "--reviewed-profile",
                str(reviewed_path),
                "--execution-atlas",
                str(atlas_path),
                "--trust-history",
                str(history_path),
                "--trust-checkpoint",
                str(checkpoint_path),
                "--expected-trust-root",
                bundle.id,
                "--expected-trust-checkpoint",
                checkpoint.id,
            ]
        )
        == 0
    )
    ledger_output = capsys.readouterr().out
    assert "execution-ledger schema=1" in ledger_output
    assert "status=completed records=7 authorizations=3 completions=3" in ledger_output
    assert "audit_evidence=unknown" in ledger_output
    assert "runtime_executable=false" in ledger_output

    anchor = rbfsafe.DeploymentTransparencyAnchor.create(
        reviewed, history, checkpoint, checkpoint.id
    )
    assert anchor.valid()
    assert anchor.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not anchor.authorizes_execution

    observation_ledger = rbfsafe.ExecutionLedger.create(
        tmp_path / "observation-ledger", session
    )
    observation_decision = observation_ledger.authorize_command(
        session,
        reviewed,
        history,
        checkpoint,
        checkpoint.id,
        atlas,
        0,
        configurations[0],
        1_000_000,
        observation_ledger.current_record_id,
    )
    assert observation_decision.authorization is not None
    independent_input = rbfsafe.IndependentRuntimeObservationInput()
    independent_input.runtime = runtime
    independent_input.observation_sequence = 8
    independent_input.observed_monotonic_ns = 1_000_001
    independent_input.monitor_state = (
        rbfsafe.ExecutionMonitorState.ARMED_CERTIFIED_SEQUENCE
    )
    independent_input.configuration_digest = "4" * 64
    independent = rbfsafe.IndependentRuntimeObservation.create(
        session,
        observation_ledger,
        observation_decision.authorization,
        independent_input,
    )
    assert independent.valid()
    assert independent.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not independent.authorizes_execution
    safety_observation = rbfsafe.sign_runtime_observation(
        independent,
        profile_safety.signer_service_id,
        profile_safety.signer_key_id,
        safety_pair.secret_key,
    )
    controls_observation = rbfsafe.sign_runtime_observation(
        independent,
        profile_controls.signer_service_id,
        profile_controls.signer_key_id,
        controls_pair.secret_key,
    )
    observation_policy = rbfsafe.RuntimeObservationPolicy()
    observation_policy.minimum_attestations = 2
    attestation_set = rbfsafe.assemble_runtime_observation_attestations(
        session,
        independent,
        observation_policy,
        [controls_observation, safety_observation],
        bundle,
    )
    rbfsafe.verify_runtime_observation_attestations(
        session, attestation_set, bundle
    )
    assert attestation_set.valid()
    assert attestation_set.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not attestation_set.authorizes_execution

    transparency_pair = rbfsafe.ed25519_key_pair_from_seed(
        bytes((index + 225) & 0xFF for index in range(32))
    )
    transparency_identity = rbfsafe.TransparencyLogIdentity.create(
        "python-deployments-v1",
        "python-transparency-log",
        "7" * 64,
        transparency_pair.public_key,
    )
    transparency_path = tmp_path / "transparency-log"
    transparency = rbfsafe.TransparencyLog.create(
        transparency_path, transparency_identity
    )
    anchor_record = transparency.publish_deployment_anchor(
        anchor, transparency_pair.secret_key, ""
    )
    observation_record = transparency.publish_runtime_observation(
        attestation_set,
        transparency_pair.secret_key,
        anchor_record.checkpoint.id,
    )
    anchor_proof = transparency.inclusion_proof(0)
    rbfsafe.verify_transparency_inclusion(
        transparency_identity,
        observation_record.checkpoint,
        anchor_record.leaf,
        anchor_proof,
    )
    consistency = transparency.consistency_witness(1)
    rbfsafe.verify_transparency_consistency(
        transparency_identity,
        anchor_record.checkpoint,
        observation_record.checkpoint,
        consistency,
    )
    compact_consistency = transparency.compact_consistency_proof(1)
    assert len(compact_consistency.old_frontier) == 1
    assert len(compact_consistency.appended_subtrees) == 1
    rbfsafe.verify_transparency_compact_consistency(
        transparency_identity,
        anchor_record.checkpoint,
        observation_record.checkpoint,
        compact_consistency,
    )

    first_safety_witness = rbfsafe.sign_transparency_checkpoint_witness(
        transparency_identity,
        anchor_record.checkpoint,
        bundle,
        safety_key.service_id,
        safety_key.id,
        safety_pair.secret_key,
    )
    first_controls_witness = rbfsafe.sign_transparency_checkpoint_witness(
        transparency_identity,
        anchor_record.checkpoint,
        bundle,
        controls_key.service_id,
        controls_key.id,
        controls_pair.secret_key,
    )
    second_safety_witness = rbfsafe.sign_transparency_checkpoint_witness(
        transparency_identity,
        observation_record.checkpoint,
        bundle,
        safety_key.service_id,
        safety_key.id,
        safety_pair.secret_key,
    )
    second_controls_witness = rbfsafe.sign_transparency_checkpoint_witness(
        transparency_identity,
        observation_record.checkpoint,
        bundle,
        controls_key.service_id,
        controls_key.id,
        controls_pair.secret_key,
    )
    witness_policy = rbfsafe.TransparencyCheckpointWitnessPolicy()
    witnessed_first = rbfsafe.assemble_witnessed_transparency_checkpoint(
        transparency_identity,
        anchor_record.checkpoint,
        witness_policy,
        [first_controls_witness, first_safety_witness],
        bundle,
    )
    witnessed_second = rbfsafe.assemble_witnessed_transparency_checkpoint(
        transparency_identity,
        observation_record.checkpoint,
        witness_policy,
        [second_controls_witness, second_safety_witness],
        bundle,
    )
    rbfsafe.verify_witnessed_transparency_checkpoint(
        transparency_identity, witnessed_second, bundle
    )
    assert witnessed_second.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not witnessed_second.authorizes_execution

    first_gossip = rbfsafe.sign_transparency_checkpoint_gossip(
        transparency_identity,
        witnessed_first,
        None,
        "python-transparency-auditor",
        1,
        "",
        bundle,
        safety_key.service_id,
        safety_key.id,
        safety_pair.secret_key,
    )
    second_gossip = rbfsafe.sign_transparency_checkpoint_gossip(
        transparency_identity,
        witnessed_second,
        compact_consistency,
        "python-transparency-auditor",
        2,
        first_gossip.id,
        bundle,
        safety_key.service_id,
        safety_key.id,
        safety_pair.secret_key,
    )
    rbfsafe.verify_transparency_checkpoint_gossip(
        transparency_identity, second_gossip, bundle
    )
    gossip_audit = rbfsafe.audit_transparency_checkpoint_gossip(
        transparency_identity, [second_gossip, first_gossip], bundle
    )
    assert gossip_audit.status == rbfsafe.TransparencyGossipStatus.CONSISTENT
    assert gossip_audit.linked_checkpoint_pairs == 1
    assert gossip_audit.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not gossip_audit.authorizes_execution

    gossip_archive_path = tmp_path / "transparency-gossip-archive"
    gossip_archive = rbfsafe.TransparencyGossipArchive.create(
        gossip_archive_path, transparency_identity, bundle
    )
    first_gossip_record = gossip_archive.publish(first_gossip, "")
    second_gossip_record = gossip_archive.publish(
        second_gossip, first_gossip_record.id
    )
    assert second_gossip_record.parent_id == first_gossip_record.id
    assert len(gossip_archive.records) == 2
    assert (
        gossip_archive.audit().status
        == rbfsafe.TransparencyGossipStatus.CONSISTENT
    )
    reopened_gossip_archive = rbfsafe.TransparencyGossipArchive.open(
        gossip_archive_path,
        transparency_identity,
        bundle,
        bundle.id,
        gossip_archive.current_record_id,
    )
    assert reopened_gossip_archive.current_record_id == second_gossip_record.id
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.TransparencyGossipArchive.open(
            gossip_archive_path,
            transparency_identity,
            bundle,
            bundle.id,
            first_gossip_record.id,
        )
    assert (
        main(
            [
                str(gossip_archive_path),
                "--transparency-namespace",
                transparency_identity.log_namespace,
                "--transparency-service-id",
                transparency_identity.signer_service_id,
                "--transparency-key-id",
                transparency_identity.signer_key_id,
                "--transparency-public-key",
                transparency_identity.signer_public_key.hex(),
                "--expected-gossip-trust-bundle",
                bundle.id,
                "--expected-gossip-head",
                gossip_archive.current_record_id,
                "--trust-history",
                str(history_path),
                "--trust-checkpoint",
                str(checkpoint_path),
                "--expected-trust-root",
                bundle.id,
                "--expected-trust-checkpoint",
                checkpoint.id,
            ]
        )
        == 0
    )
    gossip_archive_output = capsys.readouterr().out
    assert "transparency-gossip-archive schema=1" in gossip_archive_output
    assert "records=2 authenticated=2 checkpoints=2" in gossip_archive_output
    assert "status=consistent" in gossip_archive_output
    assert "audit_evidence=unknown" in gossip_archive_output
    assert "runtime_executable=false" in gossip_archive_output

    fork_transparency = rbfsafe.TransparencyLog.create(
        tmp_path / "fork-transparency-log", transparency_identity
    )
    fork_first = fork_transparency.publish_deployment_anchor(
        anchor, transparency_pair.secret_key, ""
    )
    assert fork_first.checkpoint.id == anchor_record.checkpoint.id
    fork_second = fork_transparency.publish_deployment_anchor(
        anchor, transparency_pair.secret_key, fork_first.checkpoint.id
    )
    assert fork_second.checkpoint.tree_size == observation_record.checkpoint.tree_size
    assert fork_second.checkpoint.id != observation_record.checkpoint.id
    fork_safety_witness = rbfsafe.sign_transparency_checkpoint_witness(
        transparency_identity,
        fork_second.checkpoint,
        bundle,
        safety_key.service_id,
        safety_key.id,
        safety_pair.secret_key,
    )
    fork_controls_witness = rbfsafe.sign_transparency_checkpoint_witness(
        transparency_identity,
        fork_second.checkpoint,
        bundle,
        controls_key.service_id,
        controls_key.id,
        controls_pair.secret_key,
    )
    witnessed_fork = rbfsafe.assemble_witnessed_transparency_checkpoint(
        transparency_identity,
        fork_second.checkpoint,
        witness_policy,
        [fork_controls_witness, fork_safety_witness],
        bundle,
    )
    fork_gossip = rbfsafe.sign_transparency_checkpoint_gossip(
        transparency_identity,
        witnessed_fork,
        fork_transparency.compact_consistency_proof(1),
        "python-transparency-auditor",
        1,
        "",
        bundle,
        controls_key.service_id,
        controls_key.id,
        controls_pair.secret_key,
    )
    split_gossip_audit = rbfsafe.audit_transparency_checkpoint_gossip(
        transparency_identity,
        [first_gossip, second_gossip, fork_gossip],
        bundle,
    )
    assert split_gossip_audit.status == rbfsafe.TransparencyGossipStatus.SPLIT_VIEW
    assert any(
        conflict.type
        == rbfsafe.TransparencyGossipConflictType.SAME_SIZE_EQUIVOCATION
        for conflict in split_gossip_audit.conflicts
    )
    transparency_audit = transparency.audit()
    assert transparency_audit.verified_records == 2
    assert transparency_audit.deployment_anchor_count == 1
    assert transparency_audit.runtime_observation_count == 1
    assert transparency_audit.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not transparency_audit.authorizes_execution
    reopened_transparency = rbfsafe.TransparencyLog.open(
        transparency_path,
        transparency_identity,
        transparency.current_checkpoint_id,
    )
    assert reopened_transparency.current_root_hash == transparency.current_root_hash
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.TransparencyLog.open(
            transparency_path,
            transparency_identity,
            anchor_record.checkpoint.id,
        )
    assert (
        main(
            [
                str(transparency_path),
                "--transparency-namespace",
                transparency_identity.log_namespace,
                "--transparency-service-id",
                transparency_identity.signer_service_id,
                "--transparency-key-id",
                transparency_identity.signer_key_id,
                "--transparency-public-key",
                transparency_identity.signer_public_key.hex(),
                "--expected-transparency-checkpoint",
                transparency.current_checkpoint_id,
            ]
        )
        == 0
    )
    transparency_output = capsys.readouterr().out
    assert "transparency-log schema=1" in transparency_output
    assert "records=2 deployment_anchors=1 runtime_observations=1" in transparency_output
    assert "audit_evidence=unknown" in transparency_output
    assert "runtime_executable=false" in transparency_output

    fixed_transparency_identity = rbfsafe.TransparencyLogIdentity.create(
        "rbfsafe-public-deployments-v1",
        "transparency-log",
        "02348249fded6a7cf712333d50a6318aaa1309056318af5c049e4ce296cf10e8",
        transparency_pair.public_key,
    )
    fixed_transparency = rbfsafe.TransparencyLog.open(
        Path(__file__).resolve().parents[1] / "data" / "transparency_log_schema1",
        fixed_transparency_identity,
        "86d47335bee5850b9c3a404e123d50bdb751cabee03a76de6361b8e25f03772f",
    )
    assert (
        fixed_transparency.identity.id
        == "e77f9b5d98d731c0b2e6f41486c3c6870488962aa77d5c12fca4eb5e160655d4"
    )
    assert (
        fixed_transparency.current_root_hash
        == "fe8e39f32feae84fae08a914375b1e3fec1afff94f57f97ff7955b3945c14eb1"
    )
    assert len(fixed_transparency.records) == 2

    fixture_root = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "bounded_execution_session_schema1"
    )
    fixed_checkpoint = rbfsafe.ServiceTrustCheckpoint.load(
        fixture_root / "checkpoint.json"
    )
    fixed_history = rbfsafe.ServiceTrustHistory.open(
        fixture_root / "trust-history",
        fixed_checkpoint.root_bundle_id,
        fixed_checkpoint,
        fixed_checkpoint.id,
    )
    fixed_bundle = fixed_history.current_bundle()
    fixed_gossip_archive = rbfsafe.TransparencyGossipArchive.open(
        Path(__file__).resolve().parents[1]
        / "data"
        / "transparency_gossip_archive_schema1",
        fixed_transparency_identity,
        fixed_bundle,
        fixed_bundle.id,
        "fd5ac959b484ada7ea2ce15e7cc8bccf41d8b6eaa368d9dafc2aedcdb0036514",
    )
    assert len(fixed_gossip_archive.records) == 2
    assert (
        fixed_gossip_archive.audit().status
        == rbfsafe.TransparencyGossipStatus.CONSISTENT
    )
    assert (
        main(
            [
                str(
                    Path(__file__).resolve().parents[1]
                    / "data"
                    / "transparency_gossip_archive_schema1"
                ),
                "--transparency-namespace",
                fixed_transparency_identity.log_namespace,
                "--transparency-service-id",
                fixed_transparency_identity.signer_service_id,
                "--transparency-key-id",
                fixed_transparency_identity.signer_key_id,
                "--transparency-public-key",
                fixed_transparency_identity.signer_public_key.hex(),
                "--expected-gossip-trust-bundle",
                fixed_bundle.id,
                "--expected-gossip-head",
                fixed_gossip_archive.current_record_id,
                "--trust-history",
                str(fixture_root / "trust-history"),
                "--trust-checkpoint",
                str(fixture_root / "checkpoint.json"),
                "--expected-trust-root",
                fixed_checkpoint.root_bundle_id,
                "--expected-trust-checkpoint",
                fixed_checkpoint.id,
            ]
        )
        == 0
    )
    fixed_gossip_output = capsys.readouterr().out
    assert "transparency-gossip-archive schema=1" in fixed_gossip_output
    assert "status=consistent" in fixed_gossip_output
    fixed_reviewed = rbfsafe.ReviewedDeploymentProfile.load(
        fixture_root / "profile.json",
        fixed_history,
        fixed_checkpoint,
        fixed_checkpoint.id,
    )
    fixed_atlas = rbfsafe.SafeAtlas.load(fixture_root / "atlas")
    fixed_session = rbfsafe.BoundedExecutionSession.load(
        fixture_root / "session.json",
        fixed_reviewed,
        fixed_history,
        fixed_checkpoint,
        fixed_checkpoint.id,
        fixed_atlas,
    )
    assert (
        fixed_session.id
        == "62647c557ba9dad576c9ce3035ffe496fe0c224f91432d5b290586c09e6be2df"
    )
    assert not fixed_session.authorizes_execution
    fixed_ledger = rbfsafe.ExecutionLedger.open(
        Path(__file__).resolve().parents[1] / "data" / "execution_ledger_schema1",
        fixed_session,
        fixed_reviewed,
        fixed_history,
        fixed_atlas,
    )
    assert (
        fixed_ledger.id
        == "f19c91b8f7788471691ac4d6a09861ee08188c9993e21861ad13e25e9cf99aa5"
    )
    assert (
        fixed_ledger.current_record_id
        == "ef269da4406e26a5aa7621af1ca3095392fe1ca84b2327b86804024ee2a0437b"
    )
    assert (
        fixed_ledger.audit(
            fixed_session, fixed_reviewed, fixed_history, fixed_atlas
        ).status
        == rbfsafe.ExecutionLedgerStatus.COMPLETED
    )


def test_verifiable_provenance_signing_bindings() -> None:
    subject_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    attester_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
    time_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(65, 97)))
    subject_key = rbfsafe.make_service_public_key(
        "python-subject",
        subject_pair.public_key,
        state=rbfsafe.ServiceKeyState.ACTIVE,
        allow_fetch=False,
        allow_publish=True,
    )
    attester_key = rbfsafe.make_service_public_key(
        "python-attester",
        attester_pair.public_key,
        state=rbfsafe.ServiceKeyState.ACTIVE,
        allow_fetch=False,
        allow_publish=True,
    )
    time_key = rbfsafe.make_service_public_key(
        "python-time",
        time_pair.public_key,
        state=rbfsafe.ServiceKeyState.ACTIVE,
        allow_fetch=False,
        allow_publish=True,
    )
    trust_bundle = rbfsafe.ServiceTrustBundle.create(
        1, "", [time_key, subject_key, attester_key]
    )
    adapter = rbfsafe.HardwareAttestationAdapterPin(
        "python-normalizer", "1.0.0", "application/example-attestation"
    )
    hardware_policy = rbfsafe.HardwareKeyProvenancePolicy.create(
        1,
        True,
        4,
        [rbfsafe.HardwareAttestationScope.EXECUTION_CONTROL],
        [adapter],
        [rbfsafe.HardwareAttestationAuthority(attester_key.service_id, attester_key.id)],
        ["python-vendor"],
    )
    statement_input = rbfsafe.HardwareKeyAttestationInput()
    statement_input.subject_service_id = subject_key.service_id
    statement_input.subject_key_id = subject_key.id
    statement_input.subject_public_key = subject_key.public_key
    statement_input.adapter = adapter
    statement_input.vendor_id = "python-vendor"
    statement_input.product_id = "python-device"
    statement_input.evidence_digest = "a" * 64
    statement_input.nonce_digest = "b" * 64
    statement_input.scopes = [
        rbfsafe.HardwareAttestationScope.EXECUTION_CONTROL
    ]
    statement = rbfsafe.sign_hardware_key_attestation_statement(
        statement_input,
        trust_bundle,
        attester_key.service_id,
        attester_key.id,
        attester_pair.secret_key,
    )
    rbfsafe.verify_hardware_key_attestation_statement(statement, trust_bundle)
    freshness_policy = rbfsafe.ExternalTimeFreshnessPolicy.create(
        "python-clock",
        100,
        10,
        20,
        1,
        True,
        4,
        [rbfsafe.ExternalTimeSource(time_key.service_id, time_key.id)],
    )
    time_input = rbfsafe.ExternalTimeAssertionInput()
    time_input.subject_id = subject_key.id
    time_input.clock_id = freshness_policy.clock_id
    time_input.asserted_time_ns = 1_000
    time_input.uncertainty_ns = 10
    assertion = rbfsafe.sign_external_time_assertion(
        time_input,
        trust_bundle,
        time_key.service_id,
        time_key.id,
        time_pair.secret_key,
    )
    rbfsafe.verify_external_time_assertion(assertion, trust_bundle)
    provenance = rbfsafe.VerifiableProvenanceBundle.create(
        subject_key,
        hardware_policy,
        freshness_policy,
        [statement],
        [assertion],
        trust_bundle,
    )
    audit = rbfsafe.replay_verifiable_provenance(
        provenance, trust_bundle, 1_000
    )
    assert audit.ready
    assert audit.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not audit.authorizes_execution


def test_verifiable_provenance_fixture_and_cli(
    capsys: pytest.CaptureFixture[str],
) -> None:
    fixture = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "provenance_bundle_schema1"
    )
    provenance = rbfsafe.VerifiableProvenanceBundle.load(
        fixture / "provenance.json"
    )
    assert (
        provenance.id
        == "eef49057478c4545ade19a52aee0965539956235bd46dbd480c54da77ede9690"
    )
    assert provenance.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not provenance.authorizes_execution
    checkpoint = rbfsafe.ServiceTrustCheckpoint.load(
        fixture / "checkpoint.json"
    )
    history = rbfsafe.ServiceTrustHistory.open(
        fixture / "trust-history",
        checkpoint.root_bundle_id,
        checkpoint,
        checkpoint.id,
    )
    trust_bundle = history.bundle(provenance.trust_bundle_id)
    audit = rbfsafe.replay_verifiable_provenance(
        provenance, trust_bundle, 1_000_000
    )
    assert audit.hardware.status == rbfsafe.HardwareProvenanceStatus.SATISFIED
    assert audit.hardware.authenticated_statement_count == 2
    assert audit.hardware.distinct_attester_count == 2
    assert (
        audit.freshness.status
        == rbfsafe.ExternalTimeFreshnessStatus.FRESH
    )
    assert audit.freshness.authenticated_source_count == 2
    assert audit.ready
    assert audit.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not audit.authorizes_execution
    stale = rbfsafe.replay_verifiable_provenance(
        provenance, trust_bundle, 2_000_000
    )
    assert stale.freshness.status == rbfsafe.ExternalTimeFreshnessStatus.STALE
    assert not stale.ready

    from rbfsafe.cli import main

    assert (
        main(
            [
                str(fixture / "provenance.json"),
                "--trust-history",
                str(fixture / "trust-history"),
                "--trust-checkpoint",
                str(fixture / "checkpoint.json"),
                "--expected-trust-root",
                checkpoint.root_bundle_id,
                "--expected-trust-checkpoint",
                checkpoint.id,
                "--evaluated-at-ns",
                "1000000",
            ]
        )
        == 0
    )
    output = capsys.readouterr().out
    assert "verifiable-provenance-bundle schema=1" in output
    assert "hardware_status=SATISFIED" in output
    assert "freshness_status=FRESH" in output
    assert "ready=true" in output
    assert "authorizes_execution=false" in output


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


def test_continuous_fleet_occupancy_and_cli(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    robot_path = Path(__file__).resolve().parents[1] / "data" / "planar_2r.json"
    robot = rbfsafe.SerialRobotModel.from_json(robot_path)
    waypoints = [
        rbfsafe.TimedConfiguration(0, [-0.2, 0.1]),
        rbfsafe.TimedConfiguration(32, [0.2, -0.1]),
    ]
    build_options = rbfsafe.ContinuousOccupancyBuildOptions()
    build_options.maximum_normalized_joint_width = 0.05
    first = rbfsafe.build_robot_trajectory_occupancy(
        robot,
        "python-cell-clock-v1",
        "python-cell-world",
        "arm-a",
        [-4.0, 0.0, 0.0],
        waypoints,
        build_options,
    )
    second = rbfsafe.build_robot_trajectory_occupancy(
        robot,
        "python-cell-clock-v1",
        "python-cell-world",
        "arm-b",
        [4.0, 0.0, 0.0],
        waypoints,
        build_options,
    )
    assert first.valid()
    assert second.valid()
    assert first.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not first.authorizes_execution
    rbfsafe.verify_robot_trajectory_occupancy(robot, first)

    frame = rbfsafe.DeploymentFrameBounds()
    frame.rotation = [0.0, 0.0, 1.0, 0.0, 1.0, 0.0, -1.0, 0.0, 0.0]
    frame.translation = [-4.0, 0.0, 0.0]
    frame.translation_uncertainty = [0.01, 0.02, 0.03]
    frame.angular_uncertainty_radians = 0.05
    assert frame.valid()
    assert not frame.exact()
    framed_first = rbfsafe.build_robot_trajectory_occupancy_in_frame(
        robot,
        "python-cell-clock-v1",
        "python-cell-world",
        "arm-frame-a",
        frame,
        waypoints,
        build_options,
    )
    frame.translation = [4.0, 0.0, 0.0]
    framed_second = rbfsafe.build_robot_trajectory_occupancy_in_frame(
        robot,
        "python-cell-clock-v1",
        "python-cell-world",
        "arm-frame-b",
        frame,
        waypoints,
        build_options,
    )
    assert framed_first.storage_schema == 2
    assert framed_first.algorithm_version == "2"
    assert framed_first.workspace_rotation == pytest.approx(
        [0.0, 0.0, 1.0, 0.0, 1.0, 0.0, -1.0, 0.0, 0.0]
    )
    assert framed_first.workspace_translation_uncertainty == pytest.approx(
        [0.01, 0.02, 0.03]
    )
    assert framed_first.workspace_angular_uncertainty_radians == pytest.approx(
        0.05
    )
    rbfsafe.verify_robot_trajectory_occupancy(robot, framed_first)
    framed_bundle = rbfsafe.ContinuousFleetOccupancyBundle.create(
        [framed_second, framed_first]
    )
    framed_destination = tmp_path / "continuous-occupancy-schema2.json"
    framed_bundle.save(framed_destination)
    framed_loaded = rbfsafe.ContinuousFleetOccupancyBundle.load(
        framed_destination
    )
    assert framed_loaded.storage_schema == 2
    assert framed_loaded.id == framed_bundle.id

    analysis_options = rbfsafe.ContinuousFleetOccupancyOptions()
    analysis_options.minimum_separation = 1.0
    with pytest.raises(ValueError):
        rbfsafe.analyze_continuous_fleet_occupancy(
            [first], analysis_options
        )
    report = rbfsafe.analyze_continuous_fleet_occupancy(
        [second, first], analysis_options
    )
    assert (
        report.status
        == rbfsafe.ContinuousFleetOccupancyStatus.CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES
    )
    assert not report.conflicts
    assert report.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not report.authorizes_execution

    bundle = rbfsafe.ContinuousFleetOccupancyBundle.create(
        [second, first], analysis_options
    )
    destination = tmp_path / "continuous-occupancy.json"
    bundle.save(destination)
    loaded = rbfsafe.ContinuousFleetOccupancyBundle.load(destination)
    assert loaded.id == bundle.id
    assert loaded.valid()
    assert not loaded.authorizes_execution

    fixture = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "continuous_fleet_occupancy_schema1"
    )
    fixed = rbfsafe.ContinuousFleetOccupancyBundle.load(
        fixture / "occupancy.json"
    )
    fixed_robot = rbfsafe.SerialRobotModel.from_json(fixture / "robot.json")
    assert (
        fixed.id
        == "d9a6a28c80ae86a28b996c8da954c33c725d9883a22f9f080f22d51e72be4231"
    )
    for occupancy in fixed.occupancies:
        rbfsafe.verify_robot_trajectory_occupancy(fixed_robot, occupancy)

    fixture2 = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "continuous_fleet_occupancy_schema2"
    )
    fixed2 = rbfsafe.ContinuousFleetOccupancyBundle.load(
        fixture2 / "occupancy.json"
    )
    fixed2_robot = rbfsafe.SerialRobotModel.from_json(fixture2 / "robot.json")
    assert (
        fixed2.id
        == "6030e3574db5634f60b6cf04ffc325077f944ef23d256ef7cc937fe857dce8d0"
    )
    assert (
        fixed2.report.id
        == "9a8b12df9ba0ca88142a9f44ef722a411cb33930a8986e4dd7b657d8d333053e"
    )
    assert fixed2.storage_schema == 2
    for occupancy in fixed2.occupancies:
        rbfsafe.verify_robot_trajectory_occupancy(fixed2_robot, occupancy)

    from rbfsafe.cli import main

    assert (
        main(
            [
                str(destination),
                "--occupancy-robot",
                f"arm-a={robot_path}",
                "--occupancy-robot",
                f"arm-b={robot_path}",
                "--occupancy-minimum-separation",
                "10.0",
            ]
        )
        == 0
    )
    output = capsys.readouterr().out
    assert "continuous-fleet-occupancy-bundle schema=1" in output
    assert "status=POTENTIAL_CONFLICT" in output
    assert "robot_replay_verified=true" in output
    assert "evidence=unknown" in output
    assert "authorizes_execution=false" in output
    assert main([str(framed_destination)]) == 0
    framed_output = capsys.readouterr().out
    assert "continuous-fleet-occupancy-bundle schema=2" in framed_output
    assert "rotated_frames=2 uncertain_frames=2" in framed_output
    with pytest.raises(SystemExit):
        main(
            [
                str(destination),
                "--occupancy-robot",
                f"arm-a={robot_path}",
            ]
        )


def test_authenticated_occupancy_publication(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    fixture = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "continuous_fleet_occupancy_schema2"
        / "occupancy.json"
    )
    key_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    key = rbfsafe.make_service_public_key(
        "python-occupancy-publisher",
        key_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    trust_bundle = rbfsafe.ServiceTrustBundle.create(1, "", [key])
    fixed_directory = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "occupancy_publication_schema1"
    )
    fixed_publication = rbfsafe.OccupancyPublication.load(
        fixed_directory / "publication.json"
    )
    fixed_trust_bundle = rbfsafe.ServiceTrustBundle.load(
        fixed_directory / "trust-bundle.json"
    )
    assert (
        fixed_publication.id
        == "90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251"
    )
    fixed_verification = rbfsafe.verify_continuous_fleet_occupancy_publication(
        fixture,
        fixed_publication,
        fixed_trust_bundle,
        "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher",
        fixed_trust_bundle.id,
        "",
        16,
    )
    assert (
        fixed_verification.id
        == "9910e71348c6b69609bb2026e7a7f926d27bca243bc2140a0259b7ece9d8fe09"
    )

    from rbfsafe.cli import main

    assert (
        main(
            [
                str(fixed_directory / "publication.json"),
                "--occupancy-payload",
                str(fixture),
                "--occupancy-trust-bundle",
                str(fixed_directory / "trust-bundle.json"),
                "--expected-occupancy-stream",
                "fixture-cell-occupancy-stream-v1",
                "--expected-occupancy-publisher",
                "fixture-occupancy-publisher",
                "--expected-occupancy-trust-bundle",
                fixed_trust_bundle.id,
                "--expected-occupancy-parent",
                "-",
                "--occupancy-evaluation-tick",
                "16",
            ]
        )
        == 0
    )
    cli_output = capsys.readouterr().out
    assert "continuous-fleet-occupancy-publication schema=1" in cli_output
    assert "signature_verified=true" in cli_output
    assert "payload_verified=true" in cli_output
    assert "authorizes_execution=false" in cli_output

    publication = rbfsafe.sign_continuous_fleet_occupancy_publication(
        fixture,
        trust_bundle,
        "python-cell-occupancy-stream-v1",
        "python-occupancy-publisher",
        key.id,
        key_pair.secret_key,
        1,
        "",
        0,
        32,
    )
    assert publication.valid()
    assert publication.publisher_sequence == 1
    assert publication.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not publication.authorizes_execution
    verification = rbfsafe.verify_continuous_fleet_occupancy_publication(
        fixture,
        publication,
        trust_bundle,
        "python-cell-occupancy-stream-v1",
        "python-occupancy-publisher",
        trust_bundle.id,
        "",
        16,
    )
    assert verification.valid()
    assert verification.publication_id == publication.id
    assert verification.evaluation_tick == 16
    assert verification.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not verification.authorizes_execution

    path = tmp_path / "occupancy-publication.json"
    publication.save(path)
    loaded = rbfsafe.OccupancyPublication.load(path)
    assert loaded.id == publication.id
    with pytest.raises(OSError):
        publication.save(path)
    publication.save(path, overwrite=True)
    with pytest.raises(MemoryError):
        rbfsafe.OccupancyPublication.load(path, 16)

    successor = rbfsafe.sign_continuous_fleet_occupancy_publication(
        fixture,
        trust_bundle,
        "python-cell-occupancy-stream-v1",
        "python-occupancy-publisher",
        key.id,
        key_pair.secret_key,
        2,
        publication.id,
        1,
        31,
    )
    rbfsafe.verify_occupancy_publication_successor(publication, successor)
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.verify_continuous_fleet_occupancy_publication(
            fixture,
            successor,
            trust_bundle,
            "python-cell-occupancy-stream-v1",
            "python-occupancy-publisher",
            trust_bundle.id,
            "",
            16,
        )


def test_occupancy_publication_history(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    payload = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "continuous_fleet_occupancy_schema2"
        / "occupancy.json"
    )
    fixed_history_directory = (
        Path(__file__).resolve().parents[1]
        / "data"
        / "occupancy_publication_history_schema1"
    )
    fixed_history = rbfsafe.OccupancyPublicationHistory.open(
        fixed_history_directory,
        "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher",
        "89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d",
        "90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251",
        "83a6952083ac661aacff43168473c1938e29adfe738275d2230458dd6074dfb9",
    )
    assert len(fixed_history.records) == 2
    assert (
        fixed_history.records[-1].id
        == "34fad28d5893818a1cfd79e3195e9ff757fff1764fed10fae326e7f4ef12fcf9"
    )
    assert (
        fixed_history.verify(fixed_history.current_publication_id, 31).publication_id
        == fixed_history.current_publication_id
    )
    key_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    key = rbfsafe.make_service_public_key(
        "python-history-publisher",
        key_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    trust = rbfsafe.ServiceTrustBundle.create(1, "", [key])
    root = rbfsafe.sign_continuous_fleet_occupancy_publication(
        payload,
        trust,
        "python-history-stream-v1",
        "python-history-publisher",
        key.id,
        key_pair.secret_key,
        1,
        "",
        0,
        32,
    )
    successor = rbfsafe.sign_continuous_fleet_occupancy_publication(
        payload,
        trust,
        "python-history-stream-v1",
        "python-history-publisher",
        key.id,
        key_pair.secret_key,
        2,
        root.id,
        1,
        31,
    )

    history = rbfsafe.OccupancyPublicationHistory.create(
        tmp_path / "history",
        root,
        payload,
        trust,
        "python-history-stream-v1",
        "python-history-publisher",
        trust.id,
        root.id,
    )
    root_only = rbfsafe.OccupancyPublicationHistory.create(
        tmp_path / "root-only",
        root,
        payload,
        trust,
        "python-history-stream-v1",
        "python-history-publisher",
        trust.id,
        root.id,
    )
    assert history.valid()
    assert len(history.records) == 1
    assert history.current_publication().id == root.id
    assert history.publication(root.id).id == root.id
    assert history.trust_bundle().id == trust.id
    assert history.verify(root.id, 16).publication_id == root.id
    assert history.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not history.authorizes_execution

    record = history.publish(successor, payload, root.id)
    assert record.valid()
    assert record.sequence == 2
    reopened = rbfsafe.OccupancyPublicationHistory.open(
        tmp_path / "history",
        "python-history-stream-v1",
        "python-history-publisher",
        trust.id,
        root.id,
        successor.id,
    )
    assert len(reopened.records) == 2
    assert reopened.verify(successor.id, 31).publication_id == successor.id

    identical = rbfsafe.audit_occupancy_publication_histories(history, reopened)
    assert (
        identical.relation
        == rbfsafe.OccupancyPublicationHistoryRelation.IDENTICAL
    )
    extension = rbfsafe.audit_occupancy_publication_histories(
        history, root_only
    )
    assert (
        extension.relation
        == rbfsafe.OccupancyPublicationHistoryRelation.FIRST_EXTENDS_SECOND
    )
    assert (
        rbfsafe.occupancy_publication_history_relation_name(extension.relation)
        == "first_extends_second"
    )

    fork_history = rbfsafe.OccupancyPublicationHistory.create(
        tmp_path / "fork",
        root,
        payload,
        trust,
        "python-history-stream-v1",
        "python-history-publisher",
        trust.id,
        root.id,
    )
    fork_successor = rbfsafe.sign_continuous_fleet_occupancy_publication(
        payload,
        trust,
        "python-history-stream-v1",
        "python-history-publisher",
        key.id,
        key_pair.secret_key,
        2,
        root.id,
        2,
        30,
    )
    fork_history.publish(fork_successor, payload, root.id)
    fork = rbfsafe.audit_occupancy_publication_histories(
        history, fork_history
    )
    assert fork.relation == rbfsafe.OccupancyPublicationHistoryRelation.FORKED
    assert fork.fork_detected
    assert fork.common_prefix_count == 1
    assert fork.evidence == rbfsafe.EvidenceLevel.UNKNOWN
    assert not fork.authorizes_execution

    from rbfsafe.cli import main

    assert (
        main(
            [
                str(tmp_path / "history"),
                "--expected-occupancy-stream",
                "python-history-stream-v1",
                "--expected-occupancy-publisher",
                "python-history-publisher",
                "--expected-occupancy-trust-bundle",
                trust.id,
                "--expected-occupancy-root",
                root.id,
                "--expected-occupancy-head",
                successor.id,
                "--occupancy-evaluation-tick",
                "31",
                "--compare-occupancy-history",
                str(tmp_path / "fork"),
                "--expected-compare-occupancy-head",
                fork_successor.id,
            ]
        )
        == 0
    )
    output = capsys.readouterr().out
    assert "occupancy-publication-history schema=1" in output
    assert "publications=2" in output
    assert "history_relation=forked" in output
    assert "fork_detected=true" in output
    assert "expected_head_verified=true" in output
    assert "replay_verified=true" in output
    assert "authorizes_execution=false" in output
    with pytest.raises(SystemExit):
        main(
            [
                str(tmp_path / "history"),
                "--expected-occupancy-stream",
                "python-history-stream-v1",
            ]
        )
    with pytest.raises(SystemExit):
        main(
            [
                str(tmp_path / "history"),
                "--expected-occupancy-stream",
                "python-history-stream-v1",
                "--expected-occupancy-publisher",
                "python-history-publisher",
                "--expected-occupancy-trust-bundle",
                trust.id,
                "--expected-occupancy-root",
                root.id,
                "--expected-occupancy-head",
                successor.id,
                "--trust-history",
                str(tmp_path / "unrelated-trust-history"),
            ]
        )

    options = rbfsafe.OccupancyPublicationHistoryLoadOptions()
    options.maximum_publications = 1
    with pytest.raises(MemoryError):
        rbfsafe.OccupancyPublicationHistory.open(
            tmp_path / "history",
            "python-history-stream-v1",
            "python-history-publisher",
            trust.id,
            root.id,
            successor.id,
            options,
        )
    with pytest.raises(rbfsafe.IdentityMismatchError):
        rbfsafe.OccupancyPublicationHistory.open(
            tmp_path / "history",
            "python-history-stream-v1",
            "python-history-publisher",
            trust.id,
            root.id,
            root.id,
        )
