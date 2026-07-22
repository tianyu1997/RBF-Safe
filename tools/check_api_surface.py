#!/usr/bin/env python3
"""Verify the reviewed RBF-Safe 1.x public source surface snapshot."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


def public_files(root: Path) -> list[Path]:
    headers = sorted((root / "include" / "rbfsafe").glob("*.h"))
    return headers + [root / "python" / "rbfsafe" / "__init__.py"]


def normalized_bytes(path: Path) -> bytes:
    content = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    if path.name == "version.h":
        content = re.sub(
            r'(#define RBFSAFE_VERSION_(?:MAJOR|MINOR|PATCH)) \d+',
            r"\1 @VERSION@",
            content,
        )
        content = re.sub(
            r'(#define RBFSAFE_VERSION_STRING) "[^"]+"',
            r'\1 "@VERSION@"',
            content,
        )
    return content.encode("utf-8")


def current_entries(root: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for path in public_files(root):
        relative = path.relative_to(root).as_posix()
        entries[relative] = hashlib.sha256(normalized_bytes(path)).hexdigest()
    return entries


def read_manifest(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    previous = ""
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(maxsplit=1)
        if len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0]):
            raise ValueError(f"invalid API manifest line {line_number}")
        digest, relative = fields
        if relative in entries:
            raise ValueError(f"duplicate API manifest path {relative}")
        if previous and relative <= previous:
            raise ValueError("API manifest paths must be strictly sorted")
        previous = relative
        entries[relative] = digest
    return entries


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--manifest", type=Path, default=Path("data/api_surface_v1.sha256")
    )
    parser.add_argument(
        "--emit", action="store_true", help="print the current snapshot instead of checking"
    )
    args = parser.parse_args(argv)
    root = args.root.resolve()
    current = current_entries(root)
    if args.emit:
        for relative, digest in sorted(current.items()):
            print(f"{digest}  {relative}")
        return 0

    manifest = args.manifest
    if not manifest.is_absolute():
        manifest = root / manifest
    try:
        expected = read_manifest(manifest)
    except (OSError, ValueError) as error:
        print(f"API surface manifest error: {error}", file=sys.stderr)
        return 2

    missing = sorted(expected.keys() - current.keys())
    added = sorted(current.keys() - expected.keys())
    changed = sorted(
        relative
        for relative in expected.keys() & current.keys()
        if expected[relative] != current[relative]
    )
    if missing or added or changed:
        for label, paths in (("missing", missing), ("added", added), ("changed", changed)):
            for relative in paths:
                print(f"API surface {label}: {relative}", file=sys.stderr)
        print(
            "Review the compatibility impact, then intentionally regenerate the v1 snapshot.",
            file=sys.stderr,
        )
        return 1
    print(f"API surface matches v1 snapshot ({len(current)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
