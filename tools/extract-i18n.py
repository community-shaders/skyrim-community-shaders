#!/usr/bin/env python3
"""
extract-i18n.py — Extract translatable strings from Community Shaders source code.

Scans all .cpp and .h files under src/ for T("key", "default") calls and
generates/updates en.json (the English source translation file).

Usage:
    python tools/extract-i18n.py              # Preview (dry-run)
    python tools/extract-i18n.py --write      # Write en.json
    python tools/extract-i18n.py --check      # CI mode: exit 1 if en.json is stale
    python tools/extract-i18n.py --orphans    # List keys in en.json not found in code

Workflow:
    1. Developer adds  T("key", "English text")  in source code
    2. Run:  python tools/extract-i18n.py --write
    3. Commit en.json alongside the source change
    4. Weblate picks up new/changed keys automatically
"""

import argparse
import json
import re
import sys
from pathlib import Path


def find_matching_paren(text: str, open_index: int) -> int:
    """Find the matching ')' for text[open_index] == '(' while respecting strings."""
    depth = 0
    in_string = False
    escape = False

    for index in range(open_index, len(text)):
        char = text[index]

        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue

        if char == '"':
            in_string = True
        elif char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
            if depth == 0:
                return index

    return -1


def split_top_level_args(arg_text: str) -> list[str]:
    """Split a C++ argument list on top-level commas only."""
    args = []
    current = []
    paren_depth = 0
    brace_depth = 0
    bracket_depth = 0
    in_string = False
    escape = False

    for char in arg_text:
        if in_string:
            current.append(char)
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue

        if char == '"':
            in_string = True
            current.append(char)
            continue

        if char == '(':
            paren_depth += 1
        elif char == ')':
            paren_depth -= 1
        elif char == '{':
            brace_depth += 1
        elif char == '}':
            brace_depth -= 1
        elif char == '[':
            bracket_depth += 1
        elif char == ']':
            bracket_depth -= 1
        elif char == ',' and paren_depth == 0 and brace_depth == 0 and bracket_depth == 0:
            args.append("".join(current).strip())
            current = []
            continue

        current.append(char)

    tail = "".join(current).strip()
    if tail:
        args.append(tail)
    return args


def find_project_root():
    """Find the project root by looking for CMakeLists.txt."""
    d = Path(__file__).resolve().parent.parent
    if (d / "CMakeLists.txt").exists():
        return d
    d = Path.cwd()
    while d != d.parent:
        if (d / "CMakeLists.txt").exists():
            return d
        d = d.parent
    print("Error: Could not find project root (CMakeLists.txt)", file=sys.stderr)
    sys.exit(1)


def strip_comments(source: str) -> str:
    """Remove C/C++ line comments (//) and block comments (/* */)."""
    # This regex handles string literals to avoid stripping "URLs with //"
    # Pattern: match string literals, block comments, or line comments
    pattern = re.compile(
        r'("(?:[^"\\]|\\.)*")'   # group 1: string literal — keep
        r"|(/\*.*?\*/)"          # group 2: block comment — remove
        r"|(//[^\n]*)",          # group 3: line comment — remove
        re.DOTALL
    )
    def replacer(m):
        if m.group(1):
            return m.group(1)  # keep string literals
        return " "             # replace comments with space
    return pattern.sub(replacer, source)


def extract_format_calls(clean: str, prefix: str) -> list[tuple[str, str]]:
    """Extract Format(key, args, default) calls using balanced parsing instead of regex."""
    results = []

    for match in re.finditer(r'->Format\s*\(', clean):
        open_paren = match.end() - 1
        close_paren = find_matching_paren(clean, open_paren)
        if close_paren == -1:
            continue

        args = split_top_level_args(clean[open_paren + 1:close_paren])
        if len(args) < 3:
            continue

        key_expr = args[0].strip()
        default_expr = args[-1].strip()

        key_match = re.fullmatch(r'"([^"]+)"', key_expr)
        tkey_match = re.fullmatch(r'TKEY\(\s*"([^"]+)"\s*\)', key_expr)

        if key_match:
            key = key_match.group(1)
        elif tkey_match and prefix:
            key = prefix + tkey_match.group(1)
        else:
            continue

        if not re.fullmatch(r'(?:(?:"(?:[^"\\]|\\.)*"\s*)+)', default_expr, re.DOTALL):
            continue

        results.append((key, default_expr))

    return results


def extract_strings(src_dir: Path):
    """
    Extract all T("key", "default") and Format("key", {...}, "default") strings.
    Also handles TKEY("suffix") macro expansion via I18N_KEY_PREFIX.
    Returns (dict of {key: default_text}, set of key-only keys).
    """
    strings = {}
    key_only = set()
    conflicts = []

    # T("key", "default text")
    t_pattern = re.compile(
        r'\bT\(\s*"([^"]+)"\s*,\s*'       # T("key",
        r'((?:"(?:[^"\\]|\\.)*"\s*)+)'     # one or more adjacent "string" literals
        r'\)',                               # )
        re.DOTALL
    )

    # T(TKEY("suffix"), "default text")
    tkey_pattern = re.compile(
        r'\bT\(\s*TKEY\(\s*"([^"]+)"\s*\)\s*,\s*'  # T(TKEY("suffix"),
        r'((?:"(?:[^"\\]|\\.)*"\s*)+)'               # "default text"
        r'\)',                                         # )
        re.DOTALL
    )

    # T(TKEY("suffix")) — key-only with macro
    tkey_keyonly_pattern = re.compile(
        r'\bT\(\s*TKEY\(\s*"([^"]+)"\s*\)\s*\)',
        re.DOTALL
    )

    # T("key") — key-only, no inline default
    t_keyonly_pattern = re.compile(
        r'\bT\(\s*"([^"]+)"\s*\)',
        re.DOTALL
    )

    # #define I18N_KEY_PREFIX "prefix."
    prefix_pattern = re.compile(
        r'#\s*define\s+I18N_KEY_PREFIX\s+"([^"]+)"'
    )

    def concat_string_literals(raw: str) -> str:
        """Parse concatenated C++ string literals: "a" "b" -> "ab" """
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', raw)
        return unescape_cpp_string("".join(parts))

    for ext in ("*.cpp", "*.h"):
        for filepath in src_dir.rglob(ext):
            try:
                content = filepath.read_text(encoding="utf-8", errors="replace")
            except Exception as e:
                print(f"Warning: Could not read {filepath}: {e}", file=sys.stderr)
                continue

            # Strip comments to avoid matching examples in doc comments
            clean = strip_comments(content)
            rel_path = filepath.relative_to(src_dir.parent)

            # Detect I18N_KEY_PREFIX in this file
            prefix = ""
            prefix_match = prefix_pattern.search(clean)
            if prefix_match:
                prefix = prefix_match.group(1)

            def add_string(key, default, source_path):
                if key in strings and strings[key] != default:
                    conflicts.append((key, strings[key], default, str(source_path)))
                strings[key] = default
                # If this key was previously seen without a default, it's no longer "key-only"
                if default and key in key_only:
                    key_only.discard(key)

            # Extract T("key", "default") calls (full key)
            for match in t_pattern.finditer(clean):
                key = match.group(1)
                default = concat_string_literals(match.group(2))
                add_string(key, default, rel_path)

            # Extract T(TKEY("suffix"), "default") calls (prefix + suffix)
            if prefix:
                for match in tkey_pattern.finditer(clean):
                    key = prefix + match.group(1)
                    default = concat_string_literals(match.group(2))
                    add_string(key, default, rel_path)

            # Extract ->Format(..., ..., "default") calls with balanced parsing.
            for key, default_expr in extract_format_calls(clean, prefix):
                default = concat_string_literals(default_expr)
                add_string(key, default, rel_path)

            # Track T("key") key-only calls
            for match in t_keyonly_pattern.finditer(clean):
                key = match.group(1)
                if key not in strings:
                    key_only.add(key)

            # Track T(TKEY("suffix")) key-only calls
            if prefix:
                for match in tkey_keyonly_pattern.finditer(clean):
                    key = prefix + match.group(1)
                    if key not in strings:
                        key_only.add(key)

    if conflicts:
        for key, old, new, path in conflicts:
            print(f"Warning: Key '{key}' has conflicting defaults:", file=sys.stderr)
            print(f"  Existing: {old!r}", file=sys.stderr)
            print(f"  New:      {new!r}  (in {path})", file=sys.stderr)

    return strings, key_only


def unescape_cpp_string(s: str) -> str:
    """Unescape C++ string literal escape sequences."""
    s = s.replace("\\n", "\n")
    s = s.replace("\\t", "\t")
    s = s.replace('\\"', '"')
    s = s.replace("\\\\", "\\")
    s = re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), s)
    return s


def load_existing_json(path: Path) -> dict:
    """Load existing en.json, returning only string entries (skip _meta)."""
    if not path.exists():
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return {k: v for k, v in data.items() if k != "_meta" and isinstance(v, str)}
    except Exception as e:
        print(f"Warning: Could not read {path}: {e}", file=sys.stderr)
        return {}


def build_output(strings: dict) -> dict:
    """Build the final en.json content with _meta header."""
    output = {
        "_meta": {
            "language": "English",
            "locale": "en",
            "auto_generated": True,
            "generator": "tools/extract-i18n.py",
            "note": "DO NOT EDIT MANUALLY. Run: python tools/extract-i18n.py --write"
        }
    }
    for key in sorted(strings.keys()):
        output[key] = strings[key]
    return output


def main():
    # Force UTF-8 output on Windows
    if sys.stdout.encoding != "utf-8":
        sys.stdout.reconfigure(encoding="utf-8")
    if sys.stderr.encoding != "utf-8":
        sys.stderr.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(
        description="Extract i18n strings from Community Shaders source code."
    )
    parser.add_argument("--write", action="store_true",
                        help="Write/update en.json (default: dry-run preview)")
    parser.add_argument("--check", action="store_true",
                        help="CI mode: exit 1 if en.json is out of date")
    parser.add_argument("--orphans", action="store_true",
                        help="List keys in en.json that are not in source code")
    args = parser.parse_args()

    root = find_project_root()
    src_dir = root / "src"
    en_json_path = (root / "package" / "SKSE" / "Plugins"
                    / "CommunityShaders" / "Translations" / "en.json")

    print(f"Scanning: {src_dir}")
    strings, key_only = extract_strings(src_dir)

    print(f"Found {len(strings)} strings with inline defaults")
    if key_only:
        print(f"Found {len(key_only)} key-only T() calls (no inline default):")
        for k in sorted(key_only):
            print(f"  - {k}")

    if args.orphans:
        existing = load_existing_json(en_json_path)
        orphans = set(existing.keys()) - set(strings.keys())
        if orphans:
            print(f"\n{len(orphans)} orphaned key(s) in en.json (not found in source):")
            for k in sorted(orphans):
                print(f"  - {k}")
        else:
            print("\nNo orphaned keys found.")
        return

    output = build_output(strings)
    output_text = json.dumps(output, indent=4, ensure_ascii=False) + "\n"

    if args.check:
        if en_json_path.exists():
            existing_text = en_json_path.read_text(encoding="utf-8")
            if existing_text == output_text:
                print("en.json is up to date.")
                sys.exit(0)
            else:
                print("en.json is OUT OF DATE. Run: python tools/extract-i18n.py --write",
                      file=sys.stderr)
                existing = load_existing_json(en_json_path)
                added = set(strings.keys()) - set(existing.keys())
                removed = set(existing.keys()) - set(strings.keys())
                changed = {k for k in strings if k in existing and strings[k] != existing[k]}
                if added:
                    print(f"  Added: {', '.join(sorted(added))}")
                if removed:
                    print(f"  Removed: {', '.join(sorted(removed))}")
                if changed:
                    print(f"  Changed: {', '.join(sorted(changed))}")
                sys.exit(1)
        else:
            print("en.json does not exist. Run: python tools/extract-i18n.py --write",
                  file=sys.stderr)
            sys.exit(1)

    if args.write:
        en_json_path.parent.mkdir(parents=True, exist_ok=True)
        with open(en_json_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(output_text)
        print(f"Wrote {len(strings)} strings to {en_json_path}")
    else:
        print(f"\nPreview of en.json ({len(strings)} strings):")
        print("-" * 60)
        for key in sorted(strings.keys()):
            val = strings[key]
            display = val.replace("\n", "\\n")
            if len(display) > 80:
                display = display[:77] + "..."
            print(f"  {key}: {display!r}")
        print("-" * 60)
        print(f"Use --write to generate {en_json_path}")


if __name__ == "__main__":
    main()
