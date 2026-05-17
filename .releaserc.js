module.exports = {
  // 'main' is the stable release channel. Patches to the current line land
  // here directly (cherry-picked from dev) and produce vX.Y.Z. Minors/majors
  // land here via the dev → main promotion flow in release-semantic.yaml.
  //
  // 'dev' is the integration channel and produces vX.Y.Z-rc.N prereleases.
  //
  // 'hotfix/X.Y.x' is the maintenance channel for OLDER release lines.
  // semantic-release validates it as a maintenance branch, which means it is
  // only valid once 'main' has shipped a release on a newer minor/major than
  // X.Y. Patches to the current line do NOT use this — use a fix PR to main.
  branches: [
    'main',
    { name: 'dev', prerelease: 'rc' },
    {
      name: 'hotfix/+([0-9])?(.{+([0-9]),x}).x',
      range: '${name.split("/")[1]}',
      channel: '${name.split("/")[1]}',
    },
  ],
  plugins: [
    '@semantic-release/commit-analyzer',
    '@semantic-release/release-notes-generator',
    [
      '@google/semantic-release-replace-plugin',
      {
        replacements: [
          {
            files: ['CMakeLists.txt'],
            from: 'VERSION [0-9]+\\.[0-9]+\\.[0-9]+',
            // Strip prerelease suffix so CMake gets '1.5.0' not '1.5.0-rc.1'.
            // No results assertion: stable after RC is a no-op (version already set).
            to: "VERSION ${nextRelease.version.split('-')[0]}",
          },
        ],
      },
    ],
    [
      '@semantic-release/git',
      {
        assets: ['CMakeLists.txt', 'features/**/Shaders/Features/*.ini'],
        message: 'chore(release): ${nextRelease.version} [skip ci]',
      },
    ],
    [
      '@semantic-release/github',
      {
        draftRelease: true,
        assets: [],
        successComment: false,
        failComment: false,
        releasedLabels: false,
      },
    ],
  ],
};
