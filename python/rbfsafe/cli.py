"""Inspect and optionally visualize an RBF-Safe atlas."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from . import SafeAtlas


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="rbfsafe-inspect")
    parser.add_argument("atlas", type=Path, help="atlas directory")
    parser.add_argument("--plot", type=Path, help="write a 2-D slice image")
    parser.add_argument("--query", nargs="+", type=float, metavar="Q", help="query one configuration")
    parser.add_argument("--dims", nargs=2, type=int, default=(0, 1), metavar=("X", "Y"))
    parser.add_argument("--fixed", nargs="*", type=float, help="fixed configuration for non-plotted dimensions")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
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
    if args.plot:
        from .visualize import plot_slice

        figure = plot_slice(atlas, tuple(args.dims), args.fixed)
        figure.savefig(args.plot, bbox_inches="tight", dpi=160)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
