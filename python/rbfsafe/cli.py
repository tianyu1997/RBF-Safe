"""Inspect and optionally visualize RBF-Safe Atlas or corridor data."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from . import (
    AtlasUpdater,
    AtlasVersionStore,
    HipacCorridor,
    Pose3d,
    SafeAtlas,
    SafeIkSolver,
    SafeIkStatus,
    SceneSnapshot,
    SerialRobotModel,
    TrajectoryAuditor,
    TrajectoryAuditOptions,
    TrajectoryAuditStatus,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="rbfsafe-inspect")
    parser.add_argument("atlas", type=Path, help="Atlas, version store, or corridor directory")
    parser.add_argument("--plot", type=Path, help="write a 2-D slice image")
    parser.add_argument("--query", nargs="+", type=float, metavar="Q", help="query one configuration")
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
    return parser


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
