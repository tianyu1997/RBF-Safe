"""Build a conservative robot-versus-moving-obstacle occupancy bundle."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "output",
        type=Path,
        nargs="?",
        default=Path("continuous-robot-scene-occupancy.json"),
    )
    return parser


def box(lower_x: float, upper_x: float) -> rbfsafe.WorkspaceAabb:
    return rbfsafe.WorkspaceAabb(
        [lower_x, -0.25, -0.25], [upper_x, 0.25, 0.25]
    )


def main() -> int:
    output = build_parser().parse_args().output
    if output.exists():
        raise FileExistsError(f"output already exists: {output}")
    robot = rbfsafe.SerialRobotModel(
        "planar-2r",
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
    robot_options = rbfsafe.ContinuousOccupancyBuildOptions()
    robot_options.maximum_normalized_joint_width = 0.05
    robot_occupancy = rbfsafe.build_robot_trajectory_occupancy(
        robot,
        "fixture-scene-clock-v1",
        "fixture-scene-world",
        "arm-a",
        [-4.0, 0.0, 0.0],
        [
            rbfsafe.TimedConfiguration(0, [-0.2, 0.1]),
            rbfsafe.TimedConfiguration(32, [0.2, -0.1]),
        ],
        robot_options,
    )
    rbfsafe.verify_robot_trajectory_occupancy(robot, robot_occupancy)

    obstacle_options = rbfsafe.MovingObstacleOccupancyBuildOptions()
    obstacle_options.obstacle_padding = 0.02
    obstacle = rbfsafe.build_moving_obstacle_occupancy(
        "fixture-scene-clock-v1",
        "fixture-scene-world",
        "cart-a",
        [
            rbfsafe.TimedWorkspaceAabb(0, box(5.0, 5.5)),
            rbfsafe.TimedWorkspaceAabb(16, box(6.0, 6.5)),
            rbfsafe.TimedWorkspaceAabb(32, box(5.0, 5.5)),
        ],
        obstacle_options,
    )
    rbfsafe.verify_moving_obstacle_occupancy(obstacle)
    analysis_options = rbfsafe.ContinuousRobotSceneOccupancyOptions()
    analysis_options.minimum_separation = 1.0
    bundle = rbfsafe.ContinuousRobotSceneOccupancyBundle.create(
        [robot_occupancy], [obstacle], analysis_options
    )
    bundle.save(output)
    print(f"bundle={bundle.id}")
    print(f"report={bundle.report.id}")
    print(
        "status="
        + rbfsafe.continuous_robot_scene_occupancy_status_name(
            bundle.report.status
        )
    )
    print("evidence=unknown")
    print("authorizes_execution=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
