#!/usr/bin/env python3
"""Validate documentation indexes, translations, UTF-8, and local links."""

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

_CURATED_CHINESE_GUIDES = (
    "API总览.md",
    "Atlas格式.md",
    "MoveIt2集成.md",
    "OMPL适配器.md",
    "体系结构.md",
    "动态更新.md",
    "发布流程.md",
    "安全模型.md",
    "安全记忆.md",
    "安全逆运动学.md",
    "安装指南.md",
    "快速开始.md",
    "来源说明.md",
    "版本与兼容性.md",
    "策略安全.md",
    "输入格式.md",
    "走廊与HiPaC.md",
    "轨迹审核.md",
    "路线图.md",
    "运行时安全屏障.md",
    "运动学与雅可比矩阵.md",
    "项目阅读路线.md",
)

_CHINESE_SOURCE_OVERRIDES = {
    "API总览.md": "api.md",
    "Atlas格式.md": "atlas-format.md",
    "MoveIt2集成.md": "moveit2.md",
    "OMPL适配器.md": "ompl-adapter.md",
    "体系结构.md": "architecture.md",
    "动态更新.md": "dynamic-updates.md",
    "发布流程.md": "releasing.md",
    "安全模型.md": "safety-model.md",
    "安全记忆.md": "safety-memory.md",
    "安全逆运动学.md": "safe-ik.md",
    "安装指南.md": "installation.md",
    "快速开始.md": "getting-started.md",
    "来源说明.md": "provenance.md",
    "版本与兼容性.md": "versioning.md",
    "策略安全.md": "policy-safety.md",
    "输入格式.md": "input-formats.md",
    "走廊与HiPaC.md": "corridors.md",
    "轨迹审核.md": "trajectory-auditor.md",
    "路线图.md": "roadmap.md",
    "运行时安全屏障.md": "runtime-shield.md",
    "运动学与雅可比矩阵.md": "kinematics.md",
    "项目阅读路线.md": "README.md",
}

_ROOT_CHINESE_GUIDES = (
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
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


def _markdown_files(root: Path) -> list[Path]:
    candidates = list(root.glob("*.md"))
    for directory in ("docs", "plugins", "third_party"):
        base = root / directory
        if base.is_dir() and not base.is_symlink():
            candidates.extend(base.rglob("*.md"))
    return sorted({path.resolve() for path in candidates})


def check(root: Path) -> tuple[int, int, int, int, int]:
    docs = root / "docs"
    chinese = docs / "zh-CN"
    if docs.is_symlink() or chinese.is_symlink() or not chinese.is_dir():
        raise ValueError("docs/zh-CN must be a regular directory")

    english_index_path = docs / "README.md"
    english_index = _read_utf8(english_index_path)
    if not english_index.startswith("# Documentation"):
        raise ValueError("docs/README.md has an unexpected title")
    english_names = sorted(
        path.name
        for path in docs.glob("*.md")
        if path.is_file() and path.name != "README.md"
    )
    for name in english_names:
        occurrences = english_index.count(f"({name})")
        if occurrences != 1:
            raise ValueError(
                f"English documentation index must link to {name} exactly once; "
                f"found {occurrences}"
            )

    curated_names = sorted(_CURATED_CHINESE_GUIDES)
    missing_sources = [
        _CHINESE_SOURCE_OVERRIDES.get(name, name)
        for name in curated_names
        if not (docs / _CHINESE_SOURCE_OVERRIDES.get(name, name)).is_file()
    ]
    if missing_sources:
        raise ValueError(f"curated English sources are missing: {missing_sources}")
    chinese_names = sorted(
        path.name
        for path in chinese.glob("*.md")
        if path.is_file() and path.name != "README.md"
    )
    if curated_names != chinese_names:
        missing = sorted(set(curated_names) - set(chinese_names))
        extra = sorted(set(chinese_names) - set(curated_names))
        raise ValueError(f"curated Chinese set mismatch; missing={missing}, extra={extra}")

    chinese_index_path = chinese / "README.md"
    chinese_index = _read_utf8(chinese_index_path)
    if not chinese_index.startswith("# RBF-Safe 中文文档"):
        raise ValueError("docs/zh-CN/README.md has an unexpected title")

    for name in curated_names:
        document = chinese / name
        content = _read_utf8(document)
        source_name = _CHINESE_SOURCE_OVERRIDES.get(name, name)
        if not content.startswith("# "):
            raise ValueError(f"Chinese document lacks an H1 title: {document}")
        if f"(../{source_name})" not in content:
            raise ValueError(f"Chinese document lacks its English source: {document}")
        if len(_CJK_PATTERN.findall(content)) < _MIN_CJK_CHARACTERS:
            raise ValueError(f"Chinese document has too little content: {document}")
        occurrences = chinese_index.count(f"({name})")
        if occurrences != 1:
            raise ValueError(
                f"Chinese documentation index must link to {name} exactly once; "
                f"found {occurrences}"
            )

    root_readme = _read_utf8(root / "README.md")
    chinese_readme_path = root / "README.zh-CN.md"
    chinese_readme = _read_utf8(chinese_readme_path)
    if "(README.zh-CN.md)" not in root_readme or "(README.md)" not in chinese_readme:
        raise ValueError("root README language navigation is incomplete")

    for english_name in _ROOT_CHINESE_GUIDES:
        chinese_name = f"{Path(english_name).stem}.zh-CN.md"
        english = _read_utf8(root / english_name)
        translation = _read_utf8(root / chinese_name)
        if f"({chinese_name})" not in english or f"({english_name})" not in translation:
            raise ValueError(f"root language navigation is incomplete: {english_name}")
        if len(_CJK_PATTERN.findall(translation)) < _MIN_CJK_CHARACTERS:
            raise ValueError(f"root Chinese document has too little content: {chinese_name}")

    markdown = _markdown_files(root)
    links_checked = 0
    for document in markdown:
        links_checked += _check_links(root, document, _read_utf8(document))

    return (
        len(english_names),
        len(curated_names),
        len(_ROOT_CHINESE_GUIDES),
        len(markdown),
        links_checked,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        english, chinese, root_chinese, markdown, links = check(root)
    except (OSError, ValueError) as error:
        print(f"Documentation error: {error}", file=sys.stderr)
        return 1
    print(
        f"Documentation verified: {english} English guides, "
        f"{chinese} curated Chinese guides, {root_chinese} root translations, "
        f"{markdown} Markdown files, {links} relative links"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
