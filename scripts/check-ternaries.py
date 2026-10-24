#!/usr/bin/env python3
"""Fails when a C++ source uses the conditional operator (?:).

AGENTS.md bans it: "Never use ternary operation when it's possible to just use
if-else in any coding language." clang-tidy has no check for the plain operator,
only readability-avoid-nested-conditional-operator for nested ones, so the rest
of the rule is enforced here.

Comments and the contents of string and character literals are blanked out
first, keeping offsets, so a '?' or ':' inside either is not a hit. Preprocessor
lines are skipped, and '?:' (the GNU extension) and '??' (trigraphs) are not
treated as operators.

Usage:
    scripts/check-ternaries.py             # every tracked source file
    scripts/check-ternaries.py src/ui      # a subtree
    scripts/check-ternaries.py a.cpp b.h   # named files

Exits 0 when clean, 1 when a conditional operator is found, 2 on a usage error.
"""

from __future__ import annotations

import os
import re
import sys
from typing import Iterable, Iterator

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".mm", ".m")
SKIP_DIRS = {"build", "third_party", ".git", ".codegraph", "node_modules", ".cache", ".pi", ".pi-web"}


def blank_literals_and_comments(text: str) -> str:
    """Return text of the same length with comments and literal contents blanked."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                out[i + 1] = " "
                i += 2
        elif c in ('"', "'"):
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = " "
                    if i + 1 < n:
                        out[i + 1] = " "
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                if text[i] != "\n":
                    out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def conditional_operators(path: str) -> list[tuple[int, str]]:
    """Lines that hold a conditional operator, with the 1-based line number."""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        raw = handle.read()
    if "?" not in raw:
        return []

    blanked = blank_literals_and_comments(raw)
    lines = raw.split("\n")
    hits: list[tuple[int, str]] = []
    for match in re.finditer(r"\?", blanked):
        at = match.start()
        if blanked[at : at + 2] in ("?:", "??"):
            continue
        line_start = blanked.rfind("\n", 0, at) + 1
        line_end = blanked.find("\n", at)
        if line_end < 0:
            line_end = len(blanked)
        if blanked[line_start:line_end].lstrip().startswith("#"):
            continue
        number = raw.count("\n", 0, at) + 1
        hits.append((number, lines[number - 1].strip()))
    return hits


def source_files(roots: Iterable[str]) -> Iterator[str]:
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for directory, dirs, files in os.walk(root):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("."))
            for name in sorted(files):
                if name.endswith(SOURCE_SUFFIXES):
                    yield os.path.join(directory, name)


def main(argv: list[str]) -> int:
    roots = argv[1:] or ["src", "plugins"]
    missing = [r for r in roots if not os.path.exists(r)]
    if missing:
        sys.stderr.write("check-ternaries: no such path: " + ", ".join(missing) + "\n")
        return 2

    total = 0
    for path in source_files(roots):
        hits = conditional_operators(path)
        if not hits:
            continue
        for number, text in hits:
            print(f"{path}:{number}: conditional operator (use if/else): {text}")
            total += 1

    if total:
        print(f"\n{total} conditional operator(s). AGENTS.md requires if/else.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
