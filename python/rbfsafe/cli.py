"""Inspect and optionally visualize RBF-Safe Atlas or corridor data."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from . import HipacCorridor, SafeAtlas, TrajectoryAuditor, TrajectoryAuditOptions, TrajectoryAuditStatus


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="rbfsafe-inspect")
    parser.add_argument("atlas", type=Path, help="Atlas or corridor directory")
    parser.add_argument("--plot", type=Path, help="write a 2-D slice image")
    parser.add_argument("--query", nargs="+", type=float, metavar="Q", help="query one configuration")
    parser.add_argument("--trajectory", type=Path, help="audit a JSON waypoint array")
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
    if manifest.get("format") == "rbfsafe-corridor":
        if args.plot is not None or args.trajectory is not None:
            parser.error("--plot and --trajectory apply only to Atlas directories")
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

    atlas = SafeAtlas.load(args.atlas)
    components = {region.component for region in atlas.regions}
    print(f"RBF-Safe atlas schema=1 dimension={atlas.dimension}")
    print(f"regions={len(atlas.regions)} certificates={len(atlas.certificates)} components={len(components)}")
    print(f"lect_nodes={atlas.lect.size}")
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
    if args.plot:
        from .visualize import plot_slice

        figure = plot_slice(atlas, tuple(args.dims), args.fixed)
        figure.savefig(args.plot, bbox_inches="tight", dpi=160)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
