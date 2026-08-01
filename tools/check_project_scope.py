#!/usr/bin/env python3
"""Validate traceability from project.md requirements to repository evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path, PurePosixPath

_REQUIRED_KINDS = {"public-api", "test", "documentation"}
_MAX_REQUIREMENTS = 64
_MAX_EVIDENCE_PER_REQUIREMENT = 16
_MAX_TEXT_BYTES = 4 * 1024 * 1024


def _exact_object(value: object, fields: set[str], label: str) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != fields:
        raise ValueError(f"{label} must contain exactly {sorted(fields)}")
    return value


def _relative_file(root: Path, value: object, label: str) -> tuple[str, Path]:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError(f"{label} must be a non-empty POSIX path")
    relative = PurePosixPath(value)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in relative.parts):
        raise ValueError(f"{label} must stay below the repository root")
    path = root.joinpath(*relative.parts)
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{label} is not a regular repository file: {value}")
    resolved = path.resolve(strict=True)
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValueError(f"{label} resolves outside the repository: {value}") from error
    return value, resolved


def _read_text(path: Path, label: str) -> str:
    size = path.stat().st_size
    if size > _MAX_TEXT_BYTES:
        raise ValueError(f"{label} exceeds {_MAX_TEXT_BYTES} bytes")
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeError as error:
        raise ValueError(f"{label} is not UTF-8 text") from error


def check(root: Path, manifest_path: Path, matrix_path: Path) -> tuple[int, int]:
    manifest = json.loads(_read_text(manifest_path, "project scope manifest"))
    manifest = _exact_object(
        manifest, {"schema", "source_plan", "requirements"}, "project scope manifest"
    )
    if manifest["schema"] != 1:
        raise ValueError("project scope manifest schema must be 1")
    if not isinstance(manifest["source_plan"], str) or not manifest["source_plan"]:
        raise ValueError("project scope source_plan must be non-empty")
    requirements = manifest["requirements"]
    if (
        not isinstance(requirements, list)
        or not requirements
        or len(requirements) > _MAX_REQUIREMENTS
    ):
        raise ValueError(
            f"project scope must contain 1..{_MAX_REQUIREMENTS} requirements"
        )

    matrix = _read_text(matrix_path, "project scope matrix")
    matrix_markers = re.findall(
        r"<!-- requirement: ([a-z0-9]+(?:[.-][a-z0-9]+)*) -->", matrix
    )
    if matrix.count("<!-- requirement:") != len(matrix_markers):
        raise ValueError("project scope matrix contains a malformed requirement marker")
    previous_id = ""
    evidence_count = 0
    seen_ids: set[str] = set()
    for index, raw_requirement in enumerate(requirements):
        requirement = _exact_object(
            raw_requirement,
            {"id", "claim", "evidence"},
            f"requirement {index}",
        )
        requirement_id = requirement["id"]
        if not isinstance(requirement_id, str) or not re.fullmatch(
            r"[a-z0-9]+(?:[.-][a-z0-9]+)*", requirement_id
        ):
            raise ValueError(f"requirement {index} has an invalid id")
        if requirement_id in seen_ids or (
            previous_id and requirement_id <= previous_id
        ):
            raise ValueError("requirement ids must be unique and strictly sorted")
        seen_ids.add(requirement_id)
        previous_id = requirement_id
        if not isinstance(requirement["claim"], str) or not requirement["claim"].strip():
            raise ValueError(f"requirement {requirement_id} has an empty claim")
        marker = f"<!-- requirement: {requirement_id} -->"
        if matrix.count(marker) != 1:
            raise ValueError(
                f"project scope matrix must contain exactly one marker {marker}"
            )

        evidence = requirement["evidence"]
        if (
            not isinstance(evidence, list)
            or not evidence
            or len(evidence) > _MAX_EVIDENCE_PER_REQUIREMENT
        ):
            raise ValueError(
                f"requirement {requirement_id} must contain 1.."
                f"{_MAX_EVIDENCE_PER_REQUIREMENT} evidence records"
            )
        kinds: set[str] = set()
        for evidence_index, raw_record in enumerate(evidence):
            record = _exact_object(
                raw_record,
                {"kind", "path", "contains"},
                f"requirement {requirement_id} evidence {evidence_index}",
            )
            kind = record["kind"]
            if kind not in _REQUIRED_KINDS:
                raise ValueError(
                    f"requirement {requirement_id} has unknown evidence kind {kind}"
                )
            kinds.add(kind)
            relative, path = _relative_file(
                root,
                record["path"],
                f"requirement {requirement_id} evidence path",
            )
            contains = record["contains"]
            if (
                not isinstance(contains, list)
                or not contains
                or any(
                    not isinstance(symbol, str)
                    or not symbol
                    or len(symbol) > 256
                    for symbol in contains
                )
            ):
                raise ValueError(
                    f"requirement {requirement_id} evidence {relative} has "
                    "invalid contains terms"
                )
            content = _read_text(path, f"evidence {relative}")
            for symbol in contains:
                if symbol not in content:
                    raise ValueError(
                        f"requirement {requirement_id} evidence {relative} "
                        f"does not contain {symbol!r}"
                    )
            evidence_count += 1
        missing_kinds = _REQUIRED_KINDS - kinds
        if missing_kinds:
            raise ValueError(
                f"requirement {requirement_id} lacks evidence kinds "
                f"{sorted(missing_kinds)}"
            )
    if len(matrix_markers) != len(seen_ids) or set(matrix_markers) != seen_ids:
        raise ValueError(
            "project scope matrix requirement markers do not exactly match the manifest"
        )
    return len(requirements), evidence_count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("data/project_scope_manifest.json"),
    )
    parser.add_argument(
        "--matrix",
        type=Path,
        default=Path("docs/project-scope-matrix.md"),
    )
    args = parser.parse_args(argv)
    root = args.root.resolve()
    manifest_path = args.manifest
    matrix_path = args.matrix
    if not manifest_path.is_absolute():
        manifest_path = root / manifest_path
    if not matrix_path.is_absolute():
        matrix_path = root / matrix_path
    try:
        _, checked_manifest = _relative_file(
            root,
            manifest_path.resolve().relative_to(root).as_posix(),
            "project scope manifest",
        )
        _, checked_matrix = _relative_file(
            root,
            matrix_path.resolve().relative_to(root).as_posix(),
            "project scope matrix",
        )
        requirements, evidence = check(
            root, checked_manifest, checked_matrix
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Project scope traceability error: {error}", file=sys.stderr)
        return 1
    print(
        f"Project scope traceability verified: {requirements} requirements, "
        f"{evidence} evidence records"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
