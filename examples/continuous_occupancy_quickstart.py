"""Build and persist a conservative two-robot swept-link occupancy bundle."""

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
        default=Path("continuous-fleet-occupancy.json"),
    )
    return parser


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
    trajectory = [
        rbfsafe.TimedConfiguration(0, [-0.2, 0.1]),
        rbfsafe.TimedConfiguration(32, [0.2, -0.1]),
    ]
    build_options = rbfsafe.ContinuousOccupancyBuildOptions()
    build_options.maximum_normalized_joint_width = 0.05
    first = rbfsafe.build_robot_trajectory_occupancy(
        robot,
        "fixture-cell-clock-v1",
        "fixture-cell-world",
        "arm-a",
        [-4.0, 0.0, 0.0],
        trajectory,
        build_options,
    )
    second = rbfsafe.build_robot_trajectory_occupancy(
        robot,
        "fixture-cell-clock-v1",
        "fixture-cell-world",
        "arm-b",
        [4.0, 0.0, 0.0],
        trajectory,
        build_options,
    )
    rbfsafe.verify_robot_trajectory_occupancy(robot, first)
    rbfsafe.verify_robot_trajectory_occupancy(robot, second)
    analysis_options = rbfsafe.ContinuousFleetOccupancyOptions()
    analysis_options.minimum_separation = 1.0
    bundle = rbfsafe.ContinuousFleetOccupancyBundle.create(
        [second, first], analysis_options
    )
    bundle.save(output)
    print(f"bundle={bundle.id}")
    print(f"report={bundle.report.id}")
    print(
        "status="
        + rbfsafe.continuous_fleet_occupancy_status_name(
            bundle.report.status
        )
    )
    print("evidence=unknown")
    print("authorizes_execution=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
