#!/usr/bin/env python3
"""
check-debug-sections.py — Keep feature Debug UI on Util::DebugSection.

Every feature's debug UI goes through Util::DebugSection (src/Utils/DebugSection.h) so it looks
the same everywhere and respects the CS Editor's "Show Debug sections" switch. This check fails on:

  1. A hand-rolled section header titled "Debug" (TreeNode / CollapsingHeader / SeparatorText /
     DrawSectionHeader / BeginTabItem). Use `Util::DebugSection section; if (!section) return;`.
  2. A call to Util::ShowDebugSections() outside src/Utils/DebugSection.*. The class already checks it.
  3. A BUFFER_VIEWER_NODE* macro in a function that never opens a Util::DebugSection. Buffer
     viewers are debug views.

Usage:
    python tools/check-debug-sections.py          # CI mode: exit 1 on any violation
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "src"

# The files that implement the system itself.
ALLOWED_SHOW_DEBUG = {
    "src/Utils/DebugSection.cpp",
    "src/Utils/DebugSection.h",
}

HEADER_CALL_RE = re.compile(
    r"\b(?:ImGui|Util)::(?:TreeNode|TreeNodeEx|CollapsingHeader|SeparatorText|DrawSectionHeader|BeginTabItem)\s*\("
)
DEBUG_TITLE_RE = re.compile(r'"Debug(?:\s[^"]*)?"|"[^"]*\.debug"')
SHOW_DEBUG_RE = re.compile(r"\bShowDebugSections\s*\(")
BUFFER_VIEWER_RE = re.compile(r"\bBUFFER_VIEWER_NODE\w*\s*\(")


def mask(text: str) -> str:
    """Blank comments, keeping strings and newlines (length-preserving)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_strings(text: str) -> str:
    """Blank string literal contents so braces inside strings do not count."""
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', text)


def top_level_bodies(masked: str) -> list[tuple[int, int]]:
    """(start, end) offsets of each outermost-brace region that follows a function-style ')' header."""
    code = strip_strings(masked)
    spans, depth, start = [], 0, -1
    for i, c in enumerate(code):
        if c == "{":
            if depth == 0:
                start = i
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                spans.append((start, i))
    return spans


def enclosing_function(spans: list[tuple[int, int]], masked: str, pos: int) -> tuple[int, int] | None:
    """The outermost body containing pos, widened to the enclosing function when nested in a namespace/class."""
    best = None
    for start, end in spans:
        if start <= pos <= end:
            best = (start, end)
    if best is None:
        return None
    # Descend: a namespace/class body contains many functions; narrow to the innermost function.
    start, end = best
    code = strip_strings(masked)
    while True:
        header = code[max(0, start - 200) : start]
        if re.search(r"\b(?:namespace|class|struct)\b[^;{}()]*$", header):
            inner_depth, inner_start = 0, -1
            found = None
            for i in range(start + 1, end):
                c = code[i]
                if c == "{":
                    if inner_depth == 0:
                        inner_start = i
                    inner_depth += 1
                elif c == "}":
                    inner_depth -= 1
                    if inner_depth == 0 and inner_start >= 0:
                        if inner_start <= pos <= i:
                            found = (inner_start, i)
                            break
            if not found:
                return (start, end)
            start, end = found
        else:
            return (start, end)


def first_argument(args: str) -> str:
    """The text before the first top-level comma."""
    depth, i = 0, 0
    while i < len(args):
        c = args[i]
        if c == '"':
            i += 1
            while i < len(args) and args[i] != '"':
                i += 2 if args[i] == "\\" else 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            return args[:i]
        i += 1
    return args


def scan(path: Path) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    raw = path.read_text(encoding="utf-8", errors="replace")
    masked = mask(raw)
    problems = []

    def line_of(pos: int) -> int:
        return raw.count("\n", 0, pos) + 1

    for call in ([] if rel in ALLOWED_SHOW_DEBUG else HEADER_CALL_RE.finditer(masked)):
        depth, i = 0, call.end() - 1
        while i < len(masked):
            if masked[i] == '"':
                i += 1
                while i < len(masked) and masked[i] != '"':
                    i += 2 if masked[i] == "\\" else 1
            elif masked[i] == "(":
                depth += 1
            elif masked[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if DEBUG_TITLE_RE.search(first_argument(masked[call.end() : i])):
            problems.append(
                f"{rel}:{line_of(call.start())}: hand-rolled Debug header; use `Util::DebugSection section; if (!section) return;`"
            )

    if rel not in ALLOWED_SHOW_DEBUG:
        for call in SHOW_DEBUG_RE.finditer(masked):
            problems.append(
                f"{rel}:{line_of(call.start())}: ShowDebugSections() called directly; Util::DebugSection already checks it"
            )

    if rel != "src/Utils/UI.h":
        spans = top_level_bodies(masked)
        for use in BUFFER_VIEWER_RE.finditer(masked):
            func = enclosing_function(spans, masked, use.start())
            body = masked[func[0] : func[1]] if func else ""
            header = masked[max(0, func[0] - 200) : func[0]] if func else ""
            # A sub-feature's DrawDebugSection() is drawn inside its owner's Debug section.
            in_debug_draw = re.search(r"DrawDebugSection\s*\([^)]*\)\s*(?:const)?\s*(?:override)?\s*$", header)
            if "DebugSection" not in body and not in_debug_draw:
                problems.append(
                    f"{rel}:{line_of(use.start())}: buffer viewer outside a Debug section; draw it from DrawDebugSection()"
                )
    return problems


def main() -> int:
    problems = []
    for path in sorted(SRC_DIR.rglob("*")):
        if path.suffix in (".cpp", ".h"):
            problems.extend(scan(path))
    if problems:
        print("\n".join(problems))
        print(
            f"\n{len(problems)} Debug-section problem(s). See CLAUDE.md (Feature Debug sections): override "
            "HasDebugSection()/DrawDebugSection() and open the body with Util::DebugSection."
        )
        return 1
    print("OK: all Debug UI uses Util::DebugSection.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
