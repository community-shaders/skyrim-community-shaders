# Community Shaders Contribution Guidelines
If you would like to contribute to Community Shaders, you can do so by raising a pull request (PR) or raising an issue with an attached patch.

## What is considered a useful contribution

- New capability for Community Shaders
- Bug fix
- Performance improvement
- Documentation
- Code refactors

New features should be standalone pieces of work with minimal to no dependency on other existing features and offer functionality something to an end-user. Backend systems for another features are not considered features.

All contributions fall under this project's license, GPL-3.0.

## AI Usage

- AI assisted work is accepted by this project, but vibecoding is not.
- You must understand your own contribution. If a maintainer asks what your code does or why, you need to be able to explain and defend it. If you can't, it isn't ready to PR.

## PR Guidelines
- All PRs *must* follow CS code standards and style, and software development best practices.
- Do not raise PRs with AI comments. A proper explanatory comment describing something that's not obvious is fine. A dev log of why a function exists is not.
- Do not PR work-in-progress code. This means no debugging code, commented out experiments, draft code, etc. We do not allow this even if you intend to clean up with a follow-up PR. All PRs must be polished when raised, even if the feature itself is incomplete.
- Feature settings should be included in PR if applicable. Don't PR a feature that needs settings without them.
- Don't add an unreasonable number of settings for your feature. Only what is needed.
- Ideally, you'll PR with some performance numbers (CPU and GPU) in hand. This is not required to raise a PR, but without concrete numbers, it will be harder/impossible to get your work merged.
- Where possible, add profile markers.
- Work that is incomplete and actively worked on can use one of the following flags:
  - **Alpha** - high level system or a working portion of a feature
  - **Beta** - feature complete, but in need of testing and/or bugfixes
  - **Unreleased** - feature complete, but unable to be released for the time being
- Large features (+4,000 lines of code) should be broken up into smaller, digestible pieces. If you raise a large PR, it may not be reviewed at all.
  - If breaking up your large feature into smaller PRs, please ensure each PR:
    - Is polished
    - Has performance numbers
    - Sets/updates the feature flag accordingly, if applicable.

## Get in Touch
If you have any questions, or would like to discuss your potential contributions, you can reach the maintainers on Discord:

[![Discord](https://img.shields.io/discord/1080142797870485606?label=discord&logo=discord&color=5865F2)](https://discord.com/invite/nkrQybAsyy)