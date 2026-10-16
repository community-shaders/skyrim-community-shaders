---
name: upstream
description: Triage recent commits from the downstream forks alandtse/open-shaders (dev) and InTheBottle/Bottled-Shaders (Bottle-Compendium), dedupe them against community-shaders/skyrim-community-shaders dev and its open and past PRs, port what upstream needs, and open one PR per item against dev from branches on the invoking maintainer's own fork. Args - a time window ("1d", "3d", "since 2026-10-10") and/or explicit commits ("open:<sha>", "bottle:<sha>"); add "--auto" to skip the approval checkpoint.
---

# /upstream: port fork commits into dev

Port only what upstream needs, and record why for every decision. A missed duplicate or a port that doesn't compile is worse than a skipped commit.

Any maintainer can run this skill. Branches are always pushed to **the invoking maintainer's own fork**, never to `community-shaders/skyrim-community-shaders`.

## 0. Inputs

-   **Window:** default is the last 1 day of activity on each fork. Use the fork's last commit date, not today's date.
-   **Explicit commits:** `open:<sha>` / `bottle:<sha>`. When given, triage only those (window ignored).
-   **`--auto`:** skip the checkpoint in step 3 for `port-clean` and `port-adapted` items only. `needs-decision` items are never ported under `--auto`; they are reported and wait for a maintainer's approval. Never set `--auto` for scheduled or unattended runs.

## 1. Setup

### 1a. Remotes

Work in a clone of `community-shaders/skyrim-community-shaders`. Don't assume what `origin` points to, because maintainers clone either upstream or their fork. Add explicit remotes:

```bash
ensure_remote() {  # name url: add the remote, or stop if the name already points elsewhere
    if url=$(git remote get-url "$1" 2>/dev/null); then
        [ "${url%.git}" = "$2" ] || { echo "remote $1 is $url, expected $2" >&2; return 1; }
    else
        git remote add "$1" "$2"
    fi
}
ensure_remote upstream https://github.com/community-shaders/skyrim-community-shaders &&
ensure_remote open     https://github.com/alandtse/open-shaders &&
ensure_remote bottle   https://github.com/InTheBottle/Bottled-Shaders &&
git fetch upstream dev && git fetch open dev && git fetch bottle Bottle-Compendium
```

On a mismatch, stop and ask the maintainer. Never fetch or push through an unexpected remote.

Patch checks in step 2 need a clean tree at `upstream/dev`, whatever branch the maintainer has checked out. Create a scratch worktree once and run every `git apply --check` inside it:

```bash
git worktree add --detach "$SCRATCH/upstream-dev" upstream/dev   # $SCRATCH: a temp dir outside the clone
# refresh after a fetch: git -C "$SCRATCH/upstream-dev" checkout --detach upstream/dev
```

### 1b. Resolve the maintainer's fork

1. Get the authenticated GitHub login, from the GitHub MCP `get_me` or `gh api user --jq .login`.
2. Find that login's fork of `community-shaders/skyrim-community-shaders`. The fork's name may differ from upstream's (e.g. `*-main`), so look it up rather than guessing:
    - an existing git remote whose URL owner is the login, or
    - `gh api repos/community-shaders/skyrim-community-shaders/forks --paginate --jq '.[] | select(.owner.login=="<login>") | .full_name'`, or
    - the GitHub MCP `search_repositories` with `fork:true user:<login> skyrim-community-shaders`.
3. Add it with `ensure_remote fork https://github.com/<login>/<fork-repo>` and remember that exact URL as `FORK_URL`.
4. If no fork exists, stop and ask whether to create one. Never fall back to pushing to upstream.
5. **Guard:** before every push, check that every URL from `git remote get-url --push --all fork` equals `FORK_URL` (ignoring a trailing `.git`) and that each URL's owner is the login. A `pushurl` can differ from the fetch URL and a remote can hold several, so check them all. Stop on any mismatch.

### 1c. Tools (Linux-safe)

```bash
pip install -q clang-format==22.1.4 pyyaml   # matches the pre-commit clang-format rev
pip install -q git+https://github.com/alandtse/hlslkit.git   # buffer-scan only; compile needs Windows fxc
```

## 2. Triage, for every candidate commit

### 2a. Collect

-   Anchor the window to each fork's tip, so a fork that has been quiet for a few days still yields its last day of commits:

    ```bash
    tip=$(git log -1 --format=%cI <remote/branch>)
    cutoff=$(date -u -d "$tip - 1 day" +%Y-%m-%dT%H:%M:%SZ)   # use the requested window; "since <date>" is used as-is
    git log --first-parent --since="$cutoff" <remote/branch>
    ```

-   Also collect every `(sync)` commit, merges included, because `--first-parent` misses the follow-ups and any sync merge that sits on a side branch a `chore(sync)` merge brought in:

    ```bash
    git log --grep='(sync)' --since-as-filter="$cutoff" <remote/branch>
    ```

    A commit can appear in both logs, so deduplicate by SHA before triage. Triage `fix(sync)` and `refactor(sync)` commits like any other commit. `chore(sync): bump ...` version bumps are not relevant to upstream; mark them so.

-   Drop only the `chore(sync): merge upstream` commits themselves from triage, but first extract their **sync residue**: fork-side changes that rode along with the sync. Let `P` be the sync's first parent and `U` the upstream commit it pulled in (the second parent, or for a squash-style sync the commit named in its message). If a squash-style sync names no upstream commit, review the whole sync by hand instead of inferring `U` from commit dates. The expected result is the clean merge of the two, so the residue is what the sync changed beyond it:

    ```bash
    tree=$(git merge-tree --write-tree <P> <U>)   # exit 1 = conflicts: review the whole sync by hand; any other non-zero exit is a git error (bad ref?): stop and report it
    git diff "$tree" <sync-sha>                   # empty = clean sync, nothing to triage
    ```

    A non-empty diff is one candidate (source `sync <sha>`), triaged like any other commit. If `merge-tree` exits 1 (conflicts), the sync was not a clean merge, so its residue cannot be computed: review that merge's whole diff by hand (`git diff <sha>^1 <sha>`), as was needed for `9a0c2a67`. A very large diff means `U` is probably wrong: try the neighbouring upstream commit, else report it and move on.

-   For a remaining merge commit (more than one parent), use its first-parent diff everywhere below: `git diff <sha>^1 <sha>` instead of `git show <sha>`, because `git show` prints a combined diff that can hide changes. A clean merge port uses `git cherry-pick -m 1 <sha>`.
-   After filtering, group non-merge commits only when they are adjacent in the original first-parent history and touch the same area (e.g. "opt pass" + "opt fixes"). A filtered-out commit or a merge commit always ends a group, so a group's `git diff A^ B` never contains anything that was not selected. Record each group as its exact list of SHAs and triage its **combined** diff.

### 2b. Already ported or upstream?

There is no committed ledger. The record of what has been ported is the `Ported from` line in upstream PR descriptions and commit bodies (step 6), so every maintainer sees the same history. Check all of these:

1. Search upstream PRs in every state, open, closed and merged, for the source SHA (short and full) and for `<owner/repo>#<pr>`, e.g. `repo:community-shaders/skyrim-community-shaders is:pr "alandtse/open-shaders#921"`. A closed-unmerged hit means a maintainer already declined it: report it, and don't re-port it unless the commit was named explicitly.
2. Run `git log upstream/dev --grep="<sha-or-pr-ref>"` to catch ported commits.
3. Run `git show <sha> | git -C "$SCRATCH/upstream-dev" apply -R --check` (the step 1a worktree). If it succeeds, the change is already applied.
4. Compare `git patch-id --stable` against patch-ids of upstream/dev commits from the last ~2 weeks.
5. Grep upstream/dev for the commit's distinctive new identifiers and strings (new functions, members, log messages).
6. If the commit message references an upstream PR (`#NNNN` ≥ 2000), check whether that PR has merged and how its merged version differs.

### 2c. Covered by an open upstream PR?

-   List open PRs on community-shaders/skyrim-community-shaders (all authors).
-   For each PR whose **file list** overlaps the commit's files, read its diff and decide: covered, partially covered, or unrelated.
-   Also search PR titles for the commit's scope and keywords.

### 2d. Fork-only?

-   For every file the commit touches: `git cat-file -e upstream/dev:<path>`.
-   For every function, member or setting it modifies: grep upstream/dev.
-   Known fork-only areas, which are hints only and must still be verified:
    -   **open-shaders:** NeuralRendering, FoveatedRender, ShadowAtlas/ShadowScheduler/ShadowCasterManager (SCM), CSUtility, LazyShader, the VR HMD mask, DLSS stereo fallback, CommonLib bumps beyond upstream's pinned version.
    -   **Bottled:** SnowCover, ReverseZ, lite lighting variants (`NoWetness`/`NoSnowCover` flags), SkySync helpers absent upstream.
-   If a commit depends on earlier fork-only work, name that prerequisite in the reason.

### 2e. Portable? Classify the patch

-   `git show <sha> | git -C "$SCRATCH/upstream-dev" apply --check` → **clean**
-   `git apply --3way` or a hand port → **adapted**: list every place where upstream's code differs.
-   Generated or config files (shader-validation yaml, en.json) → **regenerate** against upstream; never hand-merge them.

### 2f. Size gate

Anything that adds settings, UI, shader permutations, new resources or new features is **needs-decision**. Give a size and risk estimate and don't port it without approval.

## 3. Checkpoint (skip only with --auto)

Present one triage table and wait for approval before any push.

| Fork | SHA / PR | Title | Outcome | Reason (one line, concrete) |
| ---- | -------- | ----- | ------- | --------------------------- |

Outcomes: `port-clean`, `port-adapted`, `already-upstream (#NNNN)`, `in-open-PR (#NNNN)`, `declined-before (#NNNN)`, `fork-only (<missing file/symbol>)`, `superseded`, `needs-decision (<size/risk>)`, `not-relevant (<why>)`.

## 4. Port, one item at a time

1. Branch from fresh upstream/dev: `git checkout -B port/<fork>-<slug> upstream/dev`, where `<fork>` is `open` or `bottle`. If the session requires a specific branch prefix, use `<prefix>-<slug>`. Use one branch per logical change; combine only tightly coupled items.
2. Apply every commit in the item. When it's clean, use `git cherry-pick <sha>` for a single commit. Cherry-pick keeps the original author; then rewrite the message per step 6 with `git commit --amend`. For a grouped item (step 2a), apply the group's exact recorded SHAs in order without committing, then make one commit for the whole group: `git cherry-pick -n <sha-1> <sha-2> ... <sha-n>` followed by `git commit --author="<original author>"`, with a step 6 message whose `Ported from` line names every source SHA (add `Co-authored-by:` for any other original authors). Never use a `<first>^..<last>` range, which can pull in filtered or side-branch commits. A merge commit is never part of a group; cherry-pick it on its own with `-m 1`. Otherwise port the whole item by hand and commit with `git commit --author="<original author>"`.
3. **Re-derive the claim.** Write one sentence explaining why the commit's reasoning holds in _upstream's_ code. If it doesn't hold, stop and reclassify.
4. Run the C++ checklist and fix whatever applies:
    - **D3D state:** restored or unbound afterwards, with no extra per-draw state changes.
    - **Resource naming:** every new resource goes through `Util::SetResourceName` or the Buffer.h wrappers.
    - **Threading:** check render-thread vs compile-worker vs game-thread access; locks are scoped with no nested locking; no lock on hot per-draw paths unless it's justified.
    - **Failure paths:** shader compile failure disables cleanly, and Map/Create failures are handled.
    - **SE/AE/VR:** `RELOCATION_ID`/`REL::Relocate` pairs; `GetRuntimeData()` for classes whose layout differs; VR configs updated where they exist.
    - **Settings struct change:** bump the feature `.ini` version and validate inputs.
    - **i18n:** user-visible strings use `T()`/`TKEY()`.
    - **Conventions:** ImGui via RAII guards, colors and spacing from `ThemeManager::Constants`, Doxygen comments on public methods, no TODO/FIXME.
    - **Context:** restore any useful upstream comments the fork commit deleted.
5. **Adversarial re-read** of `git diff upstream/dev`: what would make CI or a reviewer reject this? Fix it before committing.

## 5. Validate (Linux-runnable)

-   `clang-format --style=file --dry-run` on touched files. Only touched lines may change; never reformat a whole file.
-   If any strings changed: `python tools/extract-i18n.py --write`, then `--check`, `--orphans`, and `python tools/sort-i18n.py --check`.
-   If a feature `.ini` or settings struct changed: `python tools/feature_version_audit.py`.
-   If YAML or JSON configs changed: load them with `yaml.safe_load` / `json.load`.
-   If shaders or registers changed: `hlslkit-buffer-scan --features-dir features/`.
-   Record what could **not** be verified (C++ build, fxc shader compile, runtime behaviour).

## 6. Commit

-   Title: `type(scope): description`, ≤ 50 chars. Pick the type for its version impact: `fix`/`perf` → patch, `feat` → minor, `refactor`/`chore`/`style`/`docs`/`test` → none.
-   Body wrapped at 72: what was wrong, why the change fixes it, and how upstream differs from the fork.
-   Last body line: `Ported from <owner/repo>#<pr>` or `<owner/repo>@<sha>` (for a group, every SHA: `<owner/repo>@<sha-1>, @<sha-2>`), plus "adapted to dev" if hand-ported. Step 2b depends on this line, so never omit it.
-   Trailers: `Co-authored-by: <original author>` when you are the committer, plus any attribution trailers the session requires.
-   Never include model identifiers.

## 7. Publish

1. Re-fetch upstream/dev. If it moved, rebase the branch (it's yours and unpublished) and re-run step 5.
2. Re-check the guard from step 1b, then run `git push -u fork <branch>`. Retry up to 4 times with backoff (2s, 4s, 8s, 16s) on network errors only.
3. Open the PR to `community-shaders/skyrim-community-shaders:dev`, with head `<login>:<branch>` and `maintainer_can_modify: true`. Body sections:
    - **Summary**
    - **Source:** fork, PR/commit, original author. Include the exact `Ported from ...` line.
    - **Differences from the fork version**
    - **Verification done**
    - **Not verified:** e.g. "not built locally (Linux container); CI is the build check"
    - **Related PRs:** the sibling PRs from this run, plus any overlapping open PRs
    - **Run report:** the full step-3 triage table for this run, inside a `<details>` block, so every PR carries the whole run's decisions (ported or not). Run reports are never committed to the repo.
4. If the session can watch PRs, subscribe to each PR's activity and arm one safety-net check-in about 50 minutes later.

## 8. Report

Do not write any report or ledger files into the repository. The PR descriptions are the record.

Final message to the maintainer:

-   the triage table
-   the PR links
-   the needs-decision items with size and risk
-   everything not verified

## 9. Follow-through

-   Handle CI failures and review comments according to the PR-watching rules.
-   When every PR is green, mergeable and has no open threads, send one line ("all N waiting on reviewers") and keep only the safety-net check-in.

## Never

-   Push a branch to `community-shaders/skyrim-community-shaders`. Branches go on the maintainer's fork only.
-   Push to `main`/`dev`, force-push shared branches, or create `v*` tags.
-   Port a commit without re-deriving its claim against upstream code.
-   Hand-merge generated files or reformat untouched code.
-   Commit run reports, ledgers or other per-run artifacts.
-   Skip the checkpoint on scheduled or unattended runs.
