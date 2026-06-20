#!/usr/bin/env python3
"""
sort-i18n.py — Ensure non-English translation files follow en.json key order.

Reads en.json as the reference for key ordering, then checks (or rewrites)
all other translation JSON files so their keys appear in the same order.

Usage:
    python tools/sort-i18n.py                      # Preview (dry-run)
    python tools/sort-i18n.py --write              # Rewrite translation files in-place
    python tools/sort-i18n.py --write --prune-extra # Also delete keys missing from en.json
    python tools/sort-i18n.py --check              # CI mode: exit 1 if any file is mis-ordered
"""

import argparse
import json
import sys
from pathlib import Path


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


def get_en_key_order(en_path: Path) -> list[str]:
    """Load en.json and return the ordered list of translation keys (excluding _meta)."""
    with open(en_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return [k for k in data if k != "_meta"]


def sort_translation_file(data: dict, en_key_order: list[str], prune_extra: bool = False) -> dict:
    """
    Return a new ordered dict with:
      1. _meta first (if present)
      2. Keys that exist in en.json, in en.json order
      3. Any extra keys not in en.json, sorted alphabetically at the end unless pruned
    """
    en_order_set = set(en_key_order)
    sorted_data = {}

    # _meta always first
    if "_meta" in data:
        sorted_data["_meta"] = data["_meta"]

    # Keys following en.json order
    for key in en_key_order:
        if key in data:
            sorted_data[key] = data[key]

    if not prune_extra:
        # Extra keys not in en.json (sorted alphabetically)
        extra_keys = sorted(k for k in data if k != "_meta" and k not in en_order_set)
        for key in extra_keys:
            sorted_data[key] = data[key]

    return sorted_data


def get_extra_keys(data: dict, en_key_order: list[str]) -> list[str]:
    """Return keys that exist in a translation file but not in en.json."""
    en_order_set = set(en_key_order)
    return sorted(k for k in data if k != "_meta" and k not in en_order_set)


def check_order(data: dict, en_key_order: list[str]) -> bool:
    """Check if the translation file keys are already in the correct order."""
    en_order_set = set(en_key_order)
    locale_keys = [k for k in data if k != "_meta"]

    # Build expected order: en.json keys (that exist in this file) + extra keys sorted
    expected_keys = [k for k in en_key_order if k in data]
    extra_keys = sorted(k for k in data if k != "_meta" and k not in en_order_set)
    expected_keys.extend(extra_keys)

    return locale_keys == expected_keys


def main():
    # Force UTF-8 output on Windows
    if sys.stdout.encoding != "utf-8":
        sys.stdout.reconfigure(encoding="utf-8")
    if sys.stderr.encoding != "utf-8":
        sys.stderr.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(
        description="Sort translation file keys to match en.json order."
    )
    parser.add_argument("--write", action="store_true",
                        help="Rewrite translation files with correct key order")
    parser.add_argument("--prune-extra", action="store_true",
                        help="With --write, delete translation keys missing from en.json")
    parser.add_argument("--check", action="store_true",
                        help="CI mode: exit 1 if any translation file has wrong key order")
    args = parser.parse_args()

    if args.prune_extra and not args.write:
        parser.error("--prune-extra requires --write")

    root = find_project_root()
    translations_dir = (root / "package" / "SKSE" / "Plugins"
                        / "CommunityShaders" / "Translations")
    en_path = translations_dir / "en.json"

    if not en_path.exists():
        print("Error: en.json not found", file=sys.stderr)
        sys.exit(1)

    en_key_order = get_en_key_order(en_path)
    print(f"Reference: en.json ({len(en_key_order)} keys)")

    locale_files = sorted(
        p for p in translations_dir.glob("*.json") if p.name != "en.json"
    )

    if not locale_files:
        print("No translation files to check.")
        sys.exit(0)

    changed_files = []

    for path in locale_files:
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except json.JSONDecodeError as e:
            print(f"  {path.name}: SKIP (invalid JSON: {e})")
            continue

        if not isinstance(data, dict):
            print(f"  {path.name}: SKIP (root is not a JSON object)")
            continue

        extra_keys = get_extra_keys(data, en_key_order)
        needs_rewrite = not check_order(data, en_key_order)
        needs_prune = args.prune_extra and bool(extra_keys)

        if needs_rewrite:
            print(f"  {path.name}: keys are NOT in en.json order")
        elif needs_prune:
            print(f"  {path.name}: has {len(extra_keys)} key(s) missing from en.json")
        else:
            print(f"  {path.name}: OK")

        if needs_rewrite or needs_prune:
            changed_files.append(path)

            if args.write:
                sorted_data = sort_translation_file(data, en_key_order, args.prune_extra)
                output_text = json.dumps(sorted_data, indent=4, ensure_ascii=False) + "\n"
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(output_text)
                if needs_prune:
                    print(f"    -> Rewritten with correct key order; pruned {len(extra_keys)} extra key(s)")
                else:
                    print(f"    -> Rewritten with correct key order")

    print()
    if changed_files:
        if args.write:
            print(f"Fixed {len(changed_files)} file(s).")
        elif args.check:
            print(
                f"{len(changed_files)} file(s) have keys not matching en.json order:",
                file=sys.stderr
            )
            for p in changed_files:
                print(f"  - {p.name}", file=sys.stderr)
            print(
                "\nRun: python tools/sort-i18n.py --write",
                file=sys.stderr
            )
            sys.exit(1)
        else:
            print(f"{len(changed_files)} file(s) would be rewritten.")
            print("Use --write to fix them, or --check for CI validation.")
    else:
        print("All translation files are correctly ordered.")


if __name__ == "__main__":
    main()
