import os
import subprocess
import re
from pathlib import Path
import datetime
import sys
import argparse

# =====================
# Path Resolution for Project Root
# =====================
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent

# =====================
# Configuration Constants
# =====================
DEFAULT_PR_BASE_REF = "origin/dev"
DEFAULT_FEATURES_DIR = PROJECT_ROOT / "features"
DEFAULT_FEATURE_HEADERS_DIR = PROJECT_ROOT / "src/Features"
DEFAULT_NEXUS_BASE_URL = "https://www.nexusmods.com/skyrimspecialedition/mods/"
DEFAULT_SHADER_TYPES = (".ini", ".hlsl", ".hlsli")

# Regex patterns for feature metadata extraction (all DRY, only here)
RE_MOD_ID = re.compile(r'MOD_ID\s*=\s*"([^"]+)"')
RE_FEATURE_MOD_LINK_DIRECT = re.compile(r'GetFeatureModLink\s*\([^)]*\)\s*\{\s*return\s*"(https?://[^"]+)";\s*\}')
RE_FEATURE_MOD_LINK_NEXUS = re.compile(r'GetFeatureModLink\s*\([^)]*\)\s*\{\s*return\s*MakeNexusModURL\(MOD_ID\);')
RE_FEATURE_SUMMARY_DIRECT = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*(?:override)?\s*\{\s*return \{\s*"([^"]+)"\s*,\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_MULTILINE = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*(?:override)?\s*\{\s*return \{\s*((?:"[^"]*"\s*)+),\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_CPP = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*\{[^}]*?std::string description\s*=\s*"([^"]+)";\s*std::vector<std::string> keyFeatures\s*=\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_SUMMARY_CPP_MULTILINE = re.compile(r'GetFeatureSummary\s*\([^)]*\)\s*\{[^}]*?std::string description\s*=\s*((?:"[^"]*"\s*)+);\s*std::vector<std::string> keyFeatures\s*=\s*\{([^}]*)\}', re.DOTALL)
RE_FEATURE_DESCRIPTION_DIRECT = re.compile(r'GetFeatureDescription\s*\([^)]*\)\s*\{\s*return\s*"([^"]+)";\s*\}')
RE_IS_CORE = re.compile(r"IsCore\s*\(.*\)\s*const\s*override\s*\{\s*return true;\s*\}")
RE_VERSION = re.compile(r"Version\s*=\s*([0-9]+)-([0-9]+)-([0-9]+)")
RE_BUMP_SUGGESTION = re.compile(r"- \*\*(.+?)\.ini\*\*: bump to `([\d-]+)`.*\[link\]\([^)]+\) ?(?:\(([^)]+)\))?")

# Commit type regexes
RE_COMMIT_FEAT = re.compile(r"^feat(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_FIX = re.compile(r"^fix(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_REFACTOR = re.compile(r"^refactor(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_PERF = re.compile(r"^perf(\(|:|\s)", re.IGNORECASE)
RE_COMMIT_BREAKING = re.compile(r"!\s*:|BREAKING CHANGE:", re.IGNORECASE)
RE_COMMIT_NONFUNCTIONAL = re.compile(r"^(chore|docs|style|ci|test|build|refactor|perf)(\(|:|\s)", re.IGNORECASE)

# =====================
# End Configuration
# =====================

# Global state (set in main)
RELEASE_TAG = None
FEATURES_DIR = DEFAULT_FEATURES_DIR
SHADER_TYPES = DEFAULT_SHADER_TYPES

def extract_regex(pattern, content, group=1):
    m = pattern.search(content)
    return m.group(group) if m else None

def extract_multiline_strings(multiline):
    return [d.replace("\n", " ").strip() for d in re.findall(r'"([^"]*)"', multiline) if d.strip()]

def get_feature_ini(feature_path):
    ini_dir = feature_path / "Shaders" / "Features"
    if ini_dir.exists():
        for f in ini_dir.glob("*.ini"):
            return f
    return None

def get_version_from_ini(ini_path, content=None):
    if content is None:
        try:
            with open(ini_path, "r", encoding="utf-8") as f:
                content = f.read()
        except Exception:
            return None
    m = RE_VERSION.search(content)
    if m:
        return tuple(map(int, m.groups()))
    return None

def get_prior_version(ini_path, base_ref):
    # Always use path relative to project root for git show
    rel_path = os.path.relpath(ini_path, PROJECT_ROOT).replace("\\", "/")
    try:
        content = subprocess.check_output(
            ["git", "show", f"{base_ref}:{rel_path}"], stderr=subprocess.DEVNULL
        ).decode("utf-8")
        return get_version_from_ini(ini_path, content)
    except Exception:
        return None

def get_changed_files(feature_path, base_ref, file_types=None):
    if file_types is None:
        file_types = SHADER_TYPES
    rel_path = str(feature_path).replace("\\", "/")
    try:
        output = subprocess.check_output(
            ["git", "diff", "--name-status", f"{base_ref}...HEAD", "--", rel_path],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        changes = []
        for line in output.splitlines():
            status, file = line.split(maxsplit=1)
            ext = os.path.splitext(file)[1].lower()
            if ext in file_types:
                changes.append((status, file))
        return changes
    except Exception:
        return []

def get_commits_for_file(file_path, base_ref):
    try:
        output = subprocess.check_output(
            ["git", "log", f"{base_ref}..HEAD", "--pretty=%B%x1e", "--", file_path],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        return [msg.strip() for msg in output.split("\x1e") if msg.strip()]
    except Exception:
        return []

def get_bump_commit(file_path, base_ref):
    try:
        output = subprocess.check_output(
            ["git", "log", f"{base_ref}..HEAD", "--pretty=%H%x1f%B%x1e", "--", file_path],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8")
        for entry in output.split("\x1e"):
            if not entry.strip():
                continue
            parts = entry.strip().split("\x1f", 1)
            if len(parts) < 2:
                continue
            commit_hash, msg = parts
            if RE_COMMIT_FEAT.match(msg) or RE_COMMIT_FIX.match(msg) or RE_COMMIT_BREAKING.search(msg):
                return commit_hash.strip()
    except Exception:
        pass
    return None

def get_latest_commit(file_paths, base_ref):
    try:
        output = subprocess.check_output(
            ["git", "log", f"{base_ref}..HEAD", "-1", "--pretty=%H", "--", *sorted(file_paths)],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8").strip()
        return output or None
    except Exception:
        pass
    return None

def get_commit_author(commit_hash):
    try:
        output = subprocess.check_output(
            ["git", "show", "-s", "--format=%an", commit_hash],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8").strip()
        return output
    except Exception:
        return None

def apply_version_bump(ini_path, proposed_ver_str):
    try:
        with open(ini_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Replace Version = X-X-X with Version = proposed_ver_str
        new_content = RE_VERSION.sub(f"Version = {proposed_ver_str}", content, count=1)

        if new_content != content:
            with open(ini_path, "w", encoding="utf-8") as f:
                f.write(new_content)
            return True
    except Exception as e:
        print(f"Error applying bump to {ini_path}: {e}", file=sys.stderr)
    return False

def parse_feature_metadata_file(path, mod_id=None, is_core=False):
    mod_link = ""
    description = ""
    key_features = []
    with open(path, encoding="utf-8") as f:
        content = f.read()
        # modId
        if not mod_id:
            mod_id = extract_regex(RE_MOD_ID, content)
        # GetFeatureModLink
        mod_link = extract_regex(RE_FEATURE_MOD_LINK_DIRECT, content) or mod_link
        if RE_FEATURE_MOD_LINK_NEXUS.search(content) and mod_id:
            mod_link = DEFAULT_NEXUS_BASE_URL + mod_id
        if not mod_link and not is_core and mod_id:
            mod_link = DEFAULT_NEXUS_BASE_URL + mod_id
        # GetFeatureSummary
        m = RE_FEATURE_SUMMARY_DIRECT.search(content)
        if m:
            description = m.group(1).replace("\n", " ").strip()
            key_features = [k.strip().strip('"') for k in m.group(2).split(',') if k.strip()]
        m = RE_FEATURE_SUMMARY_MULTILINE.search(content)
        if m:
            description = " ".join(extract_multiline_strings(m.group(1)))
            key_features = [k.strip().strip('"') for k in m.group(2).split(',') if k.strip()]
        m = RE_FEATURE_SUMMARY_CPP.search(content)
        if m:
            description = m.group(1).replace("\n", " ").strip()
            key_features = [k.strip().strip('"') for k in m.group(2).split(',') if k.strip()]
        m = RE_FEATURE_SUMMARY_CPP_MULTILINE.search(content)
        if m:
            description = " ".join(extract_multiline_strings(m.group(1)))
            key_features = [k.strip().strip('"') for k in m.group(2).split(',') if k.strip()]
        desc_direct = extract_regex(RE_FEATURE_DESCRIPTION_DIRECT, content)
        if desc_direct and not description:
            description = desc_direct.strip()
    return {
        "mod_id": mod_id,
        "mod_link": mod_link,
        "description": description,
        "key_features": key_features
    }

def extract_feature_metadata(feature_headers_dir):
    feature_info = []
    for header in sorted(feature_headers_dir.glob("*.h")):
        name = header.stem
        short_name = None
        is_core = False
        mod_id = None
        # --- Extract from .h ---
        with open(header, encoding="utf-8") as f:
            content = f.read()
            # IsCore
            if RE_IS_CORE.search(content):
                is_core = True
            m = RE_MOD_ID.search(content)
            if m:
                mod_id = m.group(1)
        h_meta = parse_feature_metadata_file(header, mod_id=mod_id, is_core=is_core)
        # --- If missing, try .cpp ---
        cpp_path = header.with_suffix('.cpp')
        cpp_meta = {"mod_link": "", "description": "", "key_features": []}
        if cpp_path.exists() and (not h_meta["mod_link"] or not h_meta["description"] or not h_meta["key_features"]):
            cpp_meta = parse_feature_metadata_file(cpp_path, mod_id=h_meta["mod_id"], is_core=is_core)
        # Merge, preferring .h values
        mod_link = h_meta["mod_link"] or cpp_meta["mod_link"]
        description = h_meta["description"] or cpp_meta["description"]
        key_features = h_meta["key_features"] or cpp_meta["key_features"]
        feature_info.append({
            "name": name,
            "short_name": short_name,
            "is_core": is_core,
            "mod_link": mod_link,
            "description": description,
            "key_features": key_features
        })
    return feature_info

def get_latest_release_tag():
    try:
        output = subprocess.check_output([
            "git", "tag", "--list", "v*.*.*"
        ], stderr=subprocess.DEVNULL).decode("utf-8")
        tags = [t.strip() for t in output.splitlines() if re.match(r"^v\d+\.\d+\.\d+$", t.strip())]
        if not tags:
            return None
        # Sort tags by version
        def tag_key(tag):
            return tuple(map(int, tag.lstrip('v').split('.')))
        tags.sort(key=tag_key, reverse=True)
        return tags[0]
    except Exception:
        return None

def detect_pr_base():
    # 1. Use GITHUB_BASE_REF if set (GitHub Actions)
    env_base_ref = os.environ.get("GITHUB_BASE_REF")
    if env_base_ref:
        print(f"Detected PR base from GITHUB_BASE_REF: origin/{env_base_ref}", file=sys.stderr)
        return f"origin/{env_base_ref}"
    # 2. Fallback
    print(f"Falling back to {DEFAULT_PR_BASE_REF} for PR base.", file=sys.stderr)
    return DEFAULT_PR_BASE_REF

def propose_new_version(prior_version, commits):
    if not prior_version:
        return None
    major, minor, patch = prior_version
    if not commits:
        return None

    is_minor = any(RE_COMMIT_FEAT.match(c) or RE_COMMIT_BREAKING.search(c) for c in commits)
    is_patch = any(RE_COMMIT_FIX.match(c) for c in commits)
    is_nonfunctional_only = all(RE_COMMIT_NONFUNCTIONAL.match(c) for c in commits)

    if is_minor:
        return (major, minor + 1, 0)
    elif is_patch:
        return (major, minor, patch + 1)
    elif is_nonfunctional_only:
        return None
    else:
        return (major, minor, patch + 1)

def analyze_features(FEATURES_DIR, feature_meta_map, base_ref, only_changed=False):
    bump_suggestions = []
    new_features = []
    actionable = False
    feature_actions = {}
    feature_analysis = []
    # If only_changed, build a set of changed feature names
    changed_features = set()
    if only_changed:
        # Gather all changed files from the diff in both features regions
        target_dirs = [str(FEATURES_DIR), str(DEFAULT_FEATURE_HEADERS_DIR)]
        cmd = ["git", "diff", "--name-status", f"{base_ref}...HEAD", "--"] + target_dirs
        try:
            all_changes = subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode("utf-8").splitlines()
        except Exception as e:
            print(f"Error running git diff: {e}", file=sys.stderr)
            all_changes = []

        for line in all_changes:
            parts = line.split(maxsplit=1)
            if len(parts) != 2:
                continue
            status, file = parts
            file_parts = Path(file).parts

            # Case 1: features/[Feature Name]/...
            if FEATURES_DIR.name in file_parts:
                try:
                    idx = file_parts.index(FEATURES_DIR.name)
                    feature_name = file_parts[idx+1]
                    changed_features.add(feature_name)
                    continue
                except (ValueError, IndexError):
                    pass

            # Case 2: src/Features/[Feature Name].cpp or src/Features/[Feature Name]/...
            if "src" in file_parts and "Features" in file_parts:
                try:
                    idx = file_parts.index("Features")
                    name_part = file_parts[idx+1]
                    # If it's a file, strip extension. If directory, it's the feature name.
                    feature_name = os.path.splitext(name_part)[0]
                    changed_features.add(feature_name)
                    # We also add the normalized version to be safe
                    changed_features.add(''.join(feature_name.lower().split()))
                except (ValueError, IndexError):
                    pass

    # Always use GetShortName() for feature key normalization if available
    def get_feature_key(feature_dir, feature_meta_map):
        # Try to use GetShortName from metadata if present
        meta = feature_meta_map.get(''.join(feature_dir.name.lower().split()))
        if meta and 'short_name' in meta and meta['short_name']:
            return ''.join(meta['short_name'].lower().split())
        # Fallback to directory name
        return ''.join(feature_dir.name.lower().split())

    for feature_dir in FEATURES_DIR.iterdir():
        if not feature_dir.is_dir():
            continue
        feature_key = get_feature_key(feature_dir, feature_meta_map)

        # membership check
        normalized_name = ''.join(feature_dir.name.lower().split())
        if only_changed and feature_dir.name not in changed_features and feature_key not in changed_features and normalized_name not in changed_features:
            continue

        meta = feature_meta_map.get(feature_key)
        ini_path = get_feature_ini(feature_dir)
        prior_ver = get_prior_version(ini_path, base_ref) if ini_path else None
        new_ver = get_version_from_ini(ini_path) if ini_path else None

        changes = get_changed_files(feature_dir, base_ref)
        # Also check src/Features
        if meta:
            header_path = DEFAULT_FEATURE_HEADERS_DIR / (meta['name'] + ".h")
            cpp_path = DEFAULT_FEATURE_HEADERS_DIR / (meta['name'] + ".cpp")
            feature_src_dir = DEFAULT_FEATURE_HEADERS_DIR / meta['name']
            cpp_types = (".h", ".hpp", ".cpp", ".c")
            if header_path.exists():
                changes.extend(get_changed_files(header_path, base_ref, file_types=cpp_types))
            if cpp_path.exists():
                changes.extend(get_changed_files(cpp_path, base_ref, file_types=cpp_types))
            if feature_src_dir.exists() and feature_src_dir.is_dir():
                changes.extend(get_changed_files(feature_src_dir, base_ref, file_types=cpp_types))
        changes = list(set(changes))

        change_types = set(os.path.splitext(f)[1].lower() for _, f in changes)
        all_commits = []
        bump_commit = None
        bump_author = None
        for status, f in changes:
            all_commits.extend(get_commits_for_file(f, base_ref))
            if not bump_commit:
                bump_commit = get_bump_commit(f, base_ref)
                if bump_commit:
                    bump_author = get_commit_author(bump_commit)
        if not bump_commit and changes:
            any_commit = get_latest_commit([f for _, f in changes], base_ref)
            if any_commit:
                bump_commit = any_commit
                bump_author = get_commit_author(any_commit)

        proposed_ver = propose_new_version(prior_ver, all_commits) if ini_path else None
        needs_bump = (proposed_ver is not None and new_ver is not None and proposed_ver > new_ver)
        proposed_ver_str = "-".join(map(str, proposed_ver)) if proposed_ver else "-"
        prior_ver_str = "-".join(map(str, prior_ver)) if prior_ver else "-"
        new_ver_str = "-".join(map(str, new_ver)) if new_ver else "-"
        note = ""
        is_attention = False

        # Detect new feature (all files added, ini present)
        if changes and all(s == "A" for s, _ in changes):
            if ini_path:
                note = f"New feature (with ini v{new_ver_str})"
                new_features.append((feature_dir.name, new_ver_str, bump_commit))
                is_attention = True
            else:
                note = "New feature (missing ini!)"
                new_features.append((feature_dir.name, "-", bump_commit))
                is_attention = True
        # Detect new ini added
        if ini_path and prior_ver is None and new_ver is not None:
            note = f"New ini added (v{new_ver_str})"
            new_features.append((feature_dir.name, new_ver_str, bump_commit))
            is_attention = True
        # Detect files added but ini missing
        if not ini_path and any(s == "A" for s, _ in changes):
            note = "Files added, ini missing!"
            new_features.append((feature_dir.name, "-", bump_commit))
            is_attention = True

        if needs_bump:
            is_attention = True
        if is_attention:
            actionable = True

        commit_link = ""
        if bump_commit:
            author_str = f" ({bump_author})" if bump_author else ""
            commit_link = f"[link](https://github.com/doodlum/skyrim-community-shaders/commit/{bump_commit}){author_str}"

        def bold(val):
            return f"**{val}**" if is_attention and val != '' and val != '-' else val

        feature_analysis.append({
            'name': feature_dir.name,
            'prior_ver_str': prior_ver_str,
            'proposed_ver_str': proposed_ver_str,
            'needs_bump': needs_bump,
            'change_types': ', '.join(sorted(change_types)),
            'note': note,
            'commit_link': commit_link,
            'is_attention': is_attention,
            'ini_path': str(ini_path) if ini_path else None
        })
        if needs_bump:
            bump_suggestions.append(f"- **{os.path.basename(ini_path)}**: bump to `{proposed_ver_str}` {commit_link}")
        if is_attention:
            feat_act = feature_actions.setdefault(feature_dir.name, {"actions": [], "author": bump_author})
            if note:
                feat_act["actions"].append(note)
            if needs_bump:
                feat_act["actions"].append(f"Needs version bump to {proposed_ver_str}")

    return feature_analysis, bump_suggestions, new_features, actionable, feature_actions

def print_actionable_suggestions(feature_actions):
    if feature_actions:
        print("## Actionable Suggestions\n")
        for fname, info in sorted(feature_actions.items()):
            author = f" ({info['author']})" if info.get('author') else ""
            print(f"- **{fname}**{author}: " + "; ".join(info["actions"]))
    else:
        print("No actionable suggestions for changed features.")

def format_author_stats(author_stats):
    lines = []
    if author_stats:
        lines.append("\n### Author Stats\n")
        lines.append("| Author | New Features | Updated Features | Metadata Issues | Total Actionable |")
        lines.append("|--------|--------------|------------------|----------------|------------------|")
        for author, stat in sorted(author_stats.items(), key=lambda x: (-(x[1].get('new',0)+x[1].get('bump',0)+x[1].get('meta',0)), x[0])):
            total = stat.get('new',0) + stat.get('bump',0) + stat.get('meta',0)
            lines.append(f"| {author} | {stat.get('new',0)} | {stat.get('bump',0)} | {stat.get('meta',0)} | {total} |")
    return lines

def format_actionable_lines(feature_actions):
    lines = []
    if feature_actions:
        lines.append("\n## Actionable Suggestions\n")
        for fname, info in sorted(feature_actions.items()):
            author = f" ({info['author']})" if info['author'] else ""
            lines.append(f"- **{fname}**{author}: " + "; ".join(info["actions"]))
    return lines

def format_feature_table(feature_analysis):
    lines = []
    lines.append("| Feature | Prior Ver | Proposed Ver | Needs Bump | Change Types | Note | Commit |")
    lines.append("|---------|-----------|--------------|------------|--------------|------|--------|")
    def bold(val, is_attention):
        return f"**{val}**" if is_attention and val != '' and val != '-' else val
    for fa in feature_analysis:
        lines.append(f"| {bold(fa['name'], fa['is_attention'])} | {bold(fa['prior_ver_str'], fa['is_attention'])} | {bold(fa['proposed_ver_str'], fa['is_attention'])} | {bold(str(fa['needs_bump']), fa['is_attention'])} | {bold(fa['change_types'], fa['is_attention'])} | {bold(fa['note'], fa['is_attention'])} | {fa['commit_link']} |")
    return lines

def format_new_features_table(new_features, feature_meta_map, get_commit_author, normalize_name):
    lines = []
    if new_features:
        lines.append(f"### New Features Added ({len(set((n[0], n[1], n[2]) for n in new_features))})\n")
        lines.append("| Feature | INI Version | Nexus | Commit |")
        lines.append("|---------|-------------|-------|--------|")
        seen = set()
        for name, ver, commit in new_features:
            key = (name, ver, commit)
            if key in seen:
                continue
            seen.add(key)
            meta = feature_meta_map.get(normalize_name(name))
            missing = False
            if not meta or (not meta['mod_link'] and not (meta and meta['is_core'])) or not meta['description'] or not meta['key_features']:
                missing = True
            def boldmeta(val, missing=missing):
                return f"**{val}**" if missing and val != '' and val != '-' else val
            nexus_link = f"[Nexus]({meta['mod_link']})" if meta and meta['mod_link'] else ("**Missing metadata**" if not meta else "")
            author = get_commit_author(commit) if commit else None
            author_str = f" ({author})" if author else ""
            commit_link = f"[link](https://github.com/doodlum/skyrim-community-shaders/commit/{commit}){author_str}" if commit else ""
            lines.append(f"| {boldmeta(name)} | {boldmeta(ver)} | {nexus_link} | {commit_link} |")
    return lines

def format_metadata_summary(feature_metadata):
    lines = []
    lines.append("\n## Feature Metadata Summary\n")
    lines.append("| Feature | Is Core | Mod Link | Description | Key Features |")
    lines.append("|---------|---------|----------|-------------|--------------|")
    metadata_issues = []
    for info in feature_metadata:
        missing = False
        missing_fields = []
        if not info['is_core'] and not info['mod_link']:
            missing = True
            missing_fields.append('mod link')
        if not info['description']:
            missing = True
            missing_fields.append('description')
        if not info['key_features']:
            missing = True
            missing_fields.append('key features')
        def boldmeta(val, missing=missing):
            return f"**{val}**" if missing else val
        link = f"[Nexus]({info['mod_link']})" if info['mod_link'] else ""
        desc = info['description'][:80] + ("..." if len(info['description']) > 80 else "")
        keys = ", ".join(info['key_features'][:3]) + (", ..." if len(info['key_features']) > 3 else "") if info['key_features'] else ""
        lines.append(f"| {boldmeta(info['name'])} | {info['is_core']} | {link} | {desc} | {keys} |")
        if missing:
            metadata_issues.append((info['name'], missing_fields))
    return lines, metadata_issues

def build_feature_actions(bump_suggestions, metadata_issues, new_features, get_commit_author, normalize_name):
    feature_actions = {}
    for suggestion in bump_suggestions:
        m = RE_BUMP_SUGGESTION.match(suggestion)
        if m:
            fname, ver, author = m.group(1), m.group(2), m.group(3)
            if fname not in feature_actions:
                feature_actions[fname] = {"actions": [], "author": author}
            feature_actions[fname]["actions"].append(f"Bump INI version to `{ver}`")
    for name, fields in metadata_issues:
        if name not in feature_actions:
            feature_actions[name] = {"actions": [], "author": None}
        feature_actions[name]["actions"].append(f"Add: {', '.join(fields)}")
    # Mapping new features to authors
    new_features_map = {normalize_name(n): (commit, get_commit_author(commit) if commit else None) for n, _, commit in new_features}
    for name in feature_actions:
        if not feature_actions[name]["author"]:
            norm = normalize_name(name)
            if norm in new_features_map:
                commit, author = new_features_map[norm]
                if author:
                    feature_actions[name]["author"] = author
    return feature_actions

def build_author_stats(feature_actions):
    author_stats = {}
    for fname, info in feature_actions.items():
        author = info["author"]
        if not author:
            continue
        if author not in author_stats:
            author_stats[author] = {'new': 0, 'bump': 0, 'meta': 0}
        for action in info["actions"]:
            if action.startswith("Bump INI version"):
                author_stats[author]['bump'] += 1
            elif action.startswith("Add: "):
                author_stats[author]['meta'] += 1
            elif "new feature" in action.lower():
                author_stats[author]['new'] += 1
    return author_stats

def generate_audit_report(
    base_ref,
    base_date_iso,
    base_date_human,
    now,
    feature_analysis,
    new_features,
    feature_meta_map,
    get_commit_author,
    normalize_name,
    feature_metadata,
    bump_suggestions
):
    lines = []
    lines.append("# Feature Version Audit\n")
    lines.append(f"_Compared to base:_ `{base_ref}`  ")
    if base_date_iso and base_date_human:
        lines.append(f"_Base commit date:_ `{base_date_iso}` ({base_date_human})  ")
    lines.append(f"_Generated:_ `{now}`\n")
    lines.extend(format_feature_table(feature_analysis))
    lines.append("\n## Critical Information Summary\n")
    lines.extend(format_new_features_table(new_features, feature_meta_map, get_commit_author, normalize_name))
    metadata_lines, metadata_issues = format_metadata_summary(feature_metadata)
    lines.extend(metadata_lines)
    feature_actions = build_feature_actions(bump_suggestions, metadata_issues, new_features, get_commit_author, normalize_name)
    author_stats = build_author_stats(feature_actions)
    author_stats_lines = format_author_stats(author_stats)
    actionable_lines = format_actionable_lines(feature_actions)
    output = "\n".join(lines)
    output += "\n" + "\n".join(author_stats_lines)
    output += "\n" + "\n".join(actionable_lines)
    return output

def main():
    parser = argparse.ArgumentParser(description="Feature version audit for Skyrim Community Shaders.")
    parser.add_argument('--output', type=str, help='Output markdown filename')
    parser.add_argument('--ci', action='store_true', help='Exit 1 if actionable items found (alias for --fail-on-actionable)')
    parser.add_argument('--base', type=str, default=None, help='Base tag/branch/commit to compare against')
    parser.add_argument('--fail-on-actionable', action='store_true', help='Exit 1 if actionable items found (alias for --ci)')
    parser.add_argument('--pr-check', action='store_true', help='Only show actionable items for changes since base')
    parser.add_argument('--apply-bumps', action='store_true', help='Automatically apply suggested version bumps')
    args = parser.parse_args()

    if args.base:
        base_ref = args.base
    else:
        detected_base = detect_pr_base() if args.pr_check else get_latest_release_tag()
        if detected_base:
            base_ref = detected_base
        else:
            print("No valid base ref found.", file=sys.stderr)
            sys.exit(1)

    base_date_iso = None
    base_date_human = None
    try:
        base_date_iso = subprocess.check_output([
            "git", "log", "-1", "--format=%cI", base_ref
        ], stderr=subprocess.DEVNULL).decode("utf-8").strip()
        base_date_human = datetime.datetime.fromisoformat(base_date_iso.replace('Z', '+00:00')).strftime('%A, %B %d, %Y %I:%M %p')
    except Exception:
        pass

    print(f"Using base ref: {base_ref}", file=sys.stderr)
    if base_date_iso:
        print(f"Base commit date: {base_date_iso} ({base_date_human})", file=sys.stderr)

    feature_metadata = extract_feature_metadata(DEFAULT_FEATURE_HEADERS_DIR)
    def normalize_name(name): return ''.join(name.lower().split())
    feature_meta_map = {normalize_name(f['name']): f for f in feature_metadata}

    feature_analysis, bump_suggestions, new_features, actionable, feature_actions = analyze_features(
        FEATURES_DIR, feature_meta_map, base_ref, only_changed=args.pr_check)

    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    date_tag = datetime.datetime.now().strftime('%Y-%m-%d')
    output_file = args.output if args.output else (None if args.pr_check else f"feature-version-audit-{date_tag}.md")

    if args.apply_bumps:
        applied_count = 0
        for fa in feature_analysis:
            if fa['needs_bump'] and fa['ini_path']:
                if apply_version_bump(fa['ini_path'], fa['proposed_ver_str']):
                    print(f"Applied bump to {fa['name']}: {fa['prior_ver_str']} -> {fa['proposed_ver_str']}", file=sys.stderr)
                    applied_count += 1

                    fa['needs_bump'] = False

        print(f"\nSuccessfully applied {applied_count} version bumps." if applied_count > 0 else "\nNo version bumps applied.", file=sys.stderr)

        # Remove stale bump actions from feature_actions
        for fname in list(feature_actions.keys()):
            info = feature_actions[fname]
            info["actions"] = [a for a in info["actions"] if not a.startswith("Needs version bump")]
            if not info["actions"]:
                del feature_actions[fname]

        # Filter bump_suggestions for the full-report path
        bumped_ini_names = {
            os.path.basename(fa['ini_path'])
            for fa in feature_analysis
            if not fa['needs_bump'] and fa.get('ini_path')
        }
        bump_suggestions = [
            s for s in bump_suggestions
            if not any(f"**{name}**" in s for name in bumped_ini_names)
        ]

        # Recompute actionable after applying bumps
        actionable = any(fa.get('needs_bump') or "missing" in fa.get('note', '').lower() for fa in feature_analysis)
        if new_features:
            actionable = True

    if args.pr_check:
        print_actionable_suggestions(feature_actions)
    else:
        output = generate_audit_report(base_ref, base_date_iso, base_date_human, now,
                                      feature_analysis, new_features, feature_meta_map,
                                      get_commit_author, normalize_name, feature_metadata, bump_suggestions)
        if output_file:
            with open(output_file, "w", encoding="utf-8") as f: f.write(output)
        else:
            print(output)

    if actionable and (args.ci or args.fail_on_actionable):
        sys.exit(1)
    sys.exit(0)

if __name__ == "__main__":
    main()
