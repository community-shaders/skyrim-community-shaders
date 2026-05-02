const isRC = (process.env.RELEASE_TYPE || 'rc') === 'rc';

module.exports = {
  // RC:     'main' is the non-prerelease anchor required by semantic-release v25.
  //         'dev' produces rc pre-releases.
  // Stable: 'dev' is the primary release branch.
  //         'hotfix/N.N.x' maintenance branches allow patch releases from a
  //         tagged stable baseline without carrying unreleased dev work.
  //         Branch naming: hotfix/1.5.x (the x.y maintenance line, not a specific patch).
  branches: isRC
    ? ['main', { name: 'dev', prerelease: 'rc' }]
    : [
        'dev',
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
