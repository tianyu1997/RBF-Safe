from __future__ import annotations

from pathlib import Path

import pytest
import rbfsafe


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
