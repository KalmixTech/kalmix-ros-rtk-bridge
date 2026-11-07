#!/usr/bin/env python3
"""Fail when common private deployment material is present in a release tree."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_PARTS = {"build", "install", "log", "logs", ".vscode", ".idea"}
FORBIDDEN_NAMES: set[str] = set()
FORBIDDEN_SUFFIXES = {".log", ".bin", ".bag", ".db3", ".bz2", ".bzip"}
SECRET_PATTERNS = (
    re.compile(r"KALMIX_NTRIP_(?:USERNAME|PASSWORD)\s*=\s*[^\s]+"),
    re.compile(r"(?:username|password)\s*:\s*[\"'][^\"']+", re.IGNORECASE),
)
TEXT_SUFFIXES = {".md", ".txt", ".yaml", ".yml", ".py", ".cpp", ".hpp", ".h", ".xml", ".cmake"}


def is_forbidden_path(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    return (
        any(part in FORBIDDEN_PARTS for part in relative.parts)
        or path.name in FORBIDDEN_NAMES
        or path.name.endswith(".local.yaml")
        or path.suffix.lower() in FORBIDDEN_SUFFIXES
    )


def main() -> int:
    findings: list[str] = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT)
        if is_forbidden_path(path):
            findings.append(f"private or generated file: {relative}")
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            findings.append(f"non-UTF-8 text candidate: {relative}")
            continue
        for pattern in SECRET_PATTERNS:
            if pattern.search(text):
                findings.append(f"possible credential in: {relative}")
                break

    if findings:
        print("PUBLIC RELEASE CHECK FAILED:")
        print("\n".join(f"- {item}" for item in findings))
        print("Remove these files from the release tree and rotate any exposed credentials.")
        return 1

    print("Public release check passed: no configured local files, logs, archives, or obvious credentials found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
