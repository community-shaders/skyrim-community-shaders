# AI Release Notes Generation Guide for Skyrim Community Shaders

When generating release notes for Skyrim Community Shaders, follow these instructions precisely:

## Core Principles

-   **User-focused**: Prioritize features and fixes that directly impact users
-   **Clear organization**: Group by conventional commit types and feature scope
-   **Attribution**: Always include GitHub handles (@username) and PR links
-   **Concise language**: Remove technical jargon not relevant to end users

## Required Sections (in order)

1. **Features** - New user-facing functionality
2. **Fixes** - Bug fixes and issues resolved
3. **Performance** - Performance improvements
4. **Refactor** - Code improvements (minimal emphasis)
5. **Build/CI** - Infrastructure changes (minimal emphasis)
6. **Docs** - Documentation updates
7. **New Contributors** - First-time contributors
8. **Release Stats & Contributors** - Statistics and recognition
    - Basic statistics (PRs, contributors, features, bugs, time, code changes)
    - Top Feature Contributor
    - Top Fixes Contributor
    - Top Performance Contributor
    - MVP of the Release

## Section Guidelines

### Features Section

-   Use "Flatrim (SE/AE)" for non-VR features
-   Only call out VR where specifically mentioned
-   Group by feature scope when multiple related changes exist
-   Format: `Feature name ([#PR](link) by @username)`

### Fixes Section

-   Group by feature/scope (e.g., "Terrain Variation:", "UI/UX:")
-   Include closed issues: `Fix description ([#issue](link))`
-   Trace fixes to impacted features when possible

### Release Stats & Contributors Section

Include these statistics:

-   Number of PRs merged
-   Number of contributors
-   Features added
-   Bugs fixed
-   Time since last release
-   Code changes (files changed, lines added/deleted)

Then identify:

-   **Top Feature Contributor** - Based on impact, not PR count
-   **Top Fixes Contributor** - Based on critical fixes
-   **Top Performance Contributor** - Based on performance improvements
-   **MVP** - Contributor with broadest impact across categories

## Data Extraction Commands

Use these commands to gather data (avoid PowerShell, use Git Bash/WSL/cmd):

```bash
# Get closed issues
git log <last-tag>..HEAD --oneline --grep="closes #" --grep="fixes #" --grep="resolves #"

# Count PRs
git log <last-tag>..HEAD --oneline | wc -l

# Count contributors
git log <last-tag>..HEAD --format="%an" | sort | uniq | wc -l

# Get code changes
git diff --shortstat <last-tag>..HEAD

# Get contributor stats
git log <last-tag>..HEAD --format="%an" | sort | uniq -c | sort -nr
```

## AI-Specific Instructions

-   Use `--no-pager` with git commands to avoid buffer issues
-   Group related PRs by feature area, not just count
-   Prioritize user-facing impact over technical complexity
-   Skip Style/Chore changes (automated, not user-relevant)
-   Always include the full changelog link at the end
-   Use GitHub handles consistently (@username format)

## Example Structure

```
## What's Changed

### 🚀 Features
- **Flatrim (SE/AE):**
  - Sky Sync ([#1073](link) by @sicsix)
  - Weather Picker ([#1167](link) by @alandtse)

### 🐛 Fixes
- **Terrain Variation:**
  - Fixed parallax shadows ([#1124](link) by @davo0411)
- **UI/UX:**
  - Resolve alt-tab bugs ([#1196](link))

### 📊 Release Stats & Contributors
- **Number of PRs merged:** 90
- **Number of contributors:** 12
- **Features added:** 12
- **Bugs fixed:** 18
- **Time since last release:** 3 months, 5 days
- **Code changes:** 193 files changed, +74,107 lines added, -2,198 lines deleted

#### ⭐ Top Feature Contributor
- **@sicsix:** For delivering Sky Sync and Interior Sun Shadows - two major new features

#### 🐞 Top Fixes Contributor
- **@sicsix:** For critical engine and lighting fixes across multiple features

#### ⚡ Top Performance Contributor
- **@sicsix:** For Volumetric Lighting single-pass dispatch and performance improvements

#### 🏆 MVP of the Release
**@sicsix** - For delivering major features and critical fixes across all categories

**[Full Changelog](link)**
```

## Quality Checklist

-   [ ] All sections present and in correct order
-   [ ] GitHub handles used consistently
-   [ ] PR links included for all changes
-   [ ] User-facing changes prioritized
-   [ ] Statistics calculated and included
-   [ ] Top contributors identified with justification
-   [ ] Full changelog link at the end
-   [ ] No Style/Chore changes included
