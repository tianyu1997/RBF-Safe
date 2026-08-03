#!/usr/bin/env python3
"""Validate the one-to-one Simplified Chinese documentation mirror."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote

_MAX_DOCUMENT_BYTES = 4 * 1024 * 1024
_MIN_CJK_CHARACTERS = 24
_LINK_PATTERN = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
_CJK_PATTERN = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
_ROOT_MIRRORS = (
    "CHANGELOG.md",
    "CODE_OF_CONDUCT.md",
    "CONTRIBUTING.md",
    "MAINTAINERS.md",
    "SECURITY.md",
    "SUPPORT.md",
    "THIRD_PARTY_NOTICES.md",
)


def _read_utf8(path: Path) -> str:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"not a regular file: {path}")
    if path.stat().st_size > _MAX_DOCUMENT_BYTES:
        raise ValueError(f"document exceeds {_MAX_DOCUMENT_BYTES} bytes: {path}")
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeError as error:
        raise ValueError(f"document is not UTF-8: {path}") from error


def _link_path(document: Path, raw_target: str) -> Path | None:
    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    target = target.split("#", 1)[0]
    if not target or re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
        return None
    return (document.parent / unquote(target)).resolve()


def _check_links(root: Path, document: Path, content: str) -> int:
    checked = 0
    for match in _LINK_PATTERN.finditer(content):
        target = _link_path(document, match.group(1))
        if target is None:
            continue
        try:
            target.relative_to(root)
        except ValueError as error:
            raise ValueError(
                f"relative link escapes the repository in {document}: {match.group(1)}"
            ) from error
        if target.is_symlink() or not target.exists():
            raise ValueError(
                f"broken relative link in {document}: {match.group(1)}"
            )
        checked += 1
    return checked


def check(root: Path) -> tuple[int, int, int]:
    docs = root / "docs"
    chinese = docs / "zh-CN"
    if docs.is_symlink() or chinese.is_symlink() or not chinese.is_dir():
        raise ValueError("docs/zh-CN must be a regular directory")

    english_names = sorted(path.name for path in docs.glob("*.md") if path.is_file())
    chinese_names = sorted(
        path.name
        for path in chinese.glob("*.md")
        if path.is_file() and path.name != "README.md"
    )
    if english_names != chinese_names:
        missing = sorted(set(english_names) - set(chinese_names))
        extra = sorted(set(chinese_names) - set(english_names))
        raise ValueError(f"Chinese mirror mismatch; missing={missing}, extra={extra}")

    index_path = chinese / "README.md"
    index = _read_utf8(index_path)
    if not index.startswith("# RBF-Safe 中文文档"):
        raise ValueError("docs/zh-CN/README.md has an unexpected title")
    links_checked = _check_links(root, index_path, index)

    for name in english_names:
        document = chinese / name
        content = _read_utf8(document)
        if not content.startswith("# "):
            raise ValueError(f"Chinese document lacks an H1 title: {document}")
        expected_source_link = f"(../{name})"
        if expected_source_link not in content:
            raise ValueError(
                f"Chinese document lacks its English source link {expected_source_link}: "
                f"{document}"
            )
        if len(_CJK_PATTERN.findall(content)) < _MIN_CJK_CHARACTERS:
            raise ValueError(f"Chinese document has too little translated content: {document}")
        if f"({name})" not in index:
            raise ValueError(f"Chinese index does not link to {name}")
        links_checked += _check_links(root, document, content)

    root_readme = _read_utf8(root / "README.md")
    chinese_readme_path = root / "README.zh-CN.md"
    chinese_readme = _read_utf8(chinese_readme_path)
    if "(README.zh-CN.md)" not in root_readme or "(README.md)" not in chinese_readme:
        raise ValueError("root README language navigation is incomplete")
    links_checked += _check_links(root, chinese_readme_path, chinese_readme)

    for english_name in _ROOT_MIRRORS:
        english_path = root / english_name
        chinese_name = f"{Path(english_name).stem}.zh-CN.md"
        chinese_path = root / chinese_name
        english = _read_utf8(english_path)
        translation = _read_utf8(chinese_path)
        if f"({chinese_name})" not in english or f"({english_name})" not in translation:
            raise ValueError(f"root document language navigation is incomplete: {english_name}")
        if len(_CJK_PATTERN.findall(translation)) < _MIN_CJK_CHARACTERS:
            raise ValueError(f"root Chinese document has too little content: {chinese_path}")
        links_checked += _check_links(root, chinese_path, translation)
    return len(english_names), len(_ROOT_MIRRORS), links_checked


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        documents, root_guides, links = check(root)
    except (OSError, ValueError) as error:
        print(f"Chinese documentation error: {error}", file=sys.stderr)
        return 1
    print(
        f"Chinese documentation verified: {documents}/{documents} mirrors, "
        f"{root_guides} root guides, {links} relative links"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
