#!/usr/bin/env python3
"""
check-hardcoded-ui-strings.py — Find user-visible strings that bypass the translation system.

Scans .cpp/.h files under src/ for ImGui/Util widget calls whose label (or text) argument is a
bare string literal instead of T("key", "English"). Strings with no real words (format-only
strings such as "%s", "##id" suffixes, punctuation, acronyms and units) are ignored.

Existing violations are recorded per file in tools/hardcoded-ui-strings-baseline.json, so the
check ratchets: a file may not gain violations, and fixing some lets you lower its baseline.

Usage:
    python tools/check-hardcoded-ui-strings.py                 # List every violation
    python tools/check-hardcoded-ui-strings.py --check         # CI mode: exit 1 if a file exceeds its baseline
    python tools/check-hardcoded-ui-strings.py --write-baseline  # Record current counts (only ever lower them)

Suppress a deliberate literal (developer-only output, proper nouns) with a trailing
`// i18n-ignore` comment on the same line as the call's opening.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "src"
BASELINE_PATH = Path(__file__).resolve().parent / "hardcoded-ui-strings-baseline.json"
IGNORE_MARKER = "i18n-ignore"

# Widget name -> index of the argument that shows text to the user.
LABEL_ARG = {
    "Text": 0,
    "TextWrapped": 0,
    "TextDisabled": 0,
    "TextUnformatted": 0,
    "BulletText": 0,
    "TextColored": 1,
    "LabelText": 0,
    "Checkbox": 0,
    "InvertedCheckbox": 0,
    "CheckboxFlags": 0,
    "RadioButton": 0,
    "Button": 0,
    "SmallButton": 0,
    "Selectable": 0,
    "MenuItem": 0,
    "BeginMenu": 0,
    "BeginTabItem": 0,
    "TreeNode": 0,
    "TreeNodeEx": 0,
    "CollapsingHeader": 0,
    "SeparatorText": 0,
    "Combo": 0,
    "BeginCombo": 0,
    "SliderFloat": 0,
    "SliderFloat2": 0,
    "SliderFloat3": 0,
    "SliderFloat4": 0,
    "SliderInt": 0,
    "SliderInt2": 0,
    "SliderScalar": 0,
    "PercentageSlider": 0,
    "DragFloat": 0,
    "DragFloat2": 0,
    "DragFloat3": 0,
    "DragInt": 0,
    "InputText": 0,
    "InputFloat": 0,
    "InputInt": 0,
    "ColorEdit3": 0,
    "ColorEdit4": 0,
    "ProgressBar": 2,
    "TableSetupColumn": 0,
    "SetTooltip": 0,
    "SetItemTooltip": 0,
    "AddTooltip": 0,
    "DrawColorCodedValue": 0,
    "DrawSectionHeader": 0,
    "DrawDllVersionTable": 0,
}

CALL_RE = re.compile(r"\b(?:ImGui|Util)::(" + "|".join(sorted(LABEL_ARG, key=len, reverse=True)) + r")\s*\(")
FORMAT_SPEC_RE = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|L|z|j|t)?[diouxXeEfFgGaAcspn%]")
STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
WORD_RE = re.compile(r"[A-Za-z]+")


def mask_comments(text: str) -> str:
    """Blank out comments (length-preserving) while leaving string literals intact."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
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


def find_matching_paren(text: str, open_index: int) -> int:
    depth = 0
    i = open_index
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == "\\" else 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def split_args(arg_text: str) -> list[str]:
    args, depth, start, i, n = [], 0, 0, 0, len(arg_text)
    while i < n:
        c = arg_text[i]
        if c == '"':
            i += 1
            while i < n and arg_text[i] != '"':
                i += 2 if arg_text[i] == "\\" else 1
        elif c == "'":
            i += 1
            while i < n and arg_text[i] != "'":
                i += 2 if arg_text[i] == "\\" else 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            args.append(arg_text[start:i])
            start = i + 1
        i += 1
    args.append(arg_text[start:])
    return args


FORMAT_CALL_RE = re.compile(r"^(?:std|fmt)::format\s*\(")


def literal_text(arg: str) -> str | None:
    """The text of an argument made only of adjacent string literals, else None.

    `std::format("Count: {}", n)` counts too: its format string is the user-facing text.
    """
    arg = arg.strip()
    call = FORMAT_CALL_RE.match(arg)
    if call:
        close = find_matching_paren(arg, call.end() - 1)
        inner = split_args(arg[call.end() : close]) if close > 0 else []
        arg = inner[0].strip() if inner else ""
    if not arg.startswith('"'):
        return None
    pieces = STRING_LITERAL_RE.findall(arg)
    if not pieces or STRING_LITERAL_RE.sub("", arg).strip():
        return None
    return "".join(pieces)


def is_user_facing(text: str) -> bool:
    """True when the literal has real words once ids, format specifiers and escapes are removed."""
    text = text.split("##", 1)[0]
    text = FORMAT_SPEC_RE.sub(" ", text)
    text = re.sub(r"\{[^{}]*\}", " ", text)
    text = re.sub(r"\\[nrt0\\\"']", " ", text)
    # A real word has 3+ letters and a lowercase one; acronyms and units ("FPS", "RGB", "ms") do not.
    return any(len(word) >= 3 and any(c.islower() for c in word) for word in WORD_RE.findall(text))


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    """(line, widget, literal) for each hardcoded user-facing string in a file."""
    raw = path.read_text(encoding="utf-8", errors="replace")
    masked = mask_comments(raw)
    raw_lines = raw.split("\n")
    findings = []
    for call in CALL_RE.finditer(masked):
        close = find_matching_paren(masked, call.end() - 1)
        if close < 0:
            continue
        args = split_args(masked[call.end() : close])
        index = LABEL_ARG[call.group(1)]
        if index >= len(args):
            continue
        text = literal_text(args[index])
        if text is None or not is_user_facing(text):
            continue
        line = raw[: call.start()].count("\n") + 1
        if IGNORE_MARKER in raw_lines[line - 1]:
            continue
        findings.append((line, call.group(1), text))
    return findings


def scan_all() -> dict[str, list[tuple[int, str, str]]]:
    results = {}
    for path in sorted(SRC_DIR.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        findings = scan_file(path)
        if findings:
            results[path.relative_to(ROOT).as_posix()] = findings
    return results


def load_baseline() -> dict[str, int]:
    if not BASELINE_PATH.exists():
        return {}
    return json.loads(BASELINE_PATH.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="CI mode: fail if a file exceeds its baseline")
    parser.add_argument("--write-baseline", action="store_true", help="Record current per-file counts")
    args = parser.parse_args()

    results = scan_all()
    counts = {file: len(found) for file, found in results.items()}

    if args.write_baseline:
        BASELINE_PATH.write_text(json.dumps(dict(sorted(counts.items())), indent=4) + "\n", encoding="utf-8")
        print(f"Wrote baseline: {sum(counts.values())} violation(s) in {len(counts)} file(s).")
        return 0

    if not args.check:
        for file, found in results.items():
            for line, widget, text in found:
                print(f'{file}:{line}: {widget}("{text}")')
        print(f"\n{sum(counts.values())} hardcoded string(s) in {len(counts)} file(s).")
        return 0

    baseline = load_baseline()
    failed = False
    for file, found in results.items():
        allowed = baseline.get(file, 0)
        if len(found) > allowed:
            failed = True
            print(f"{file}: {len(found)} hardcoded string(s), baseline allows {allowed}:")
            for line, widget, text in found:
                print(f'  {file}:{line}: {widget}("{text}")')
    if failed:
        print(
            '\nWrap user-visible text in T("menu.page.key", "English") (see CLAUDE.md, Internationalization),\n'
            "then run `python tools/extract-i18n.py --write`. For deliberate developer-only literals add\n"
            "a trailing `// i18n-ignore` comment on the call's line."
        )
        return 1

    improved = {f: (baseline[f], counts.get(f, 0)) for f in baseline if counts.get(f, 0) < baseline[f]}
    if improved:
        print(
            f"{len(improved)} file(s) are below their baseline; run `python tools/check-hardcoded-ui-strings.py "
            "--write-baseline` to lock in the improvement."
        )
    print(f"OK: no new hardcoded UI strings ({sum(counts.values())} existing, baselined).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
