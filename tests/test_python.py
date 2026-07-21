from __future__ import annotations

import json
from pathlib import Path

import pytest
import rbfsafe


def test_version() -> None:
    assert rbfsafe.__version__ == "0.4.0"


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
