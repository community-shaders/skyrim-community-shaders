const isRC = (process.env.RELEASE_TYPE || 'rc') === 'rc';

module.exports = {
  // RC: needs 'main' as non-prerelease anchor (semantic-release v25 requires >= 1).
  // Stable: 'dev' is the release branch directly.
  branches: isRC
    ? ['main', { name: 'dev', prerelease: 'rc' }]
    : ['dev'],
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
            // Strip prerelease suffix so CMake gets '1.5.0' not '1.5.0-rc.1'
            to: "VERSION ${nextRelease.version.split('-')[0]}",
            results: [
              {
                file: 'CMakeLists.txt',
                hasChanged: true,
                numReplacements: 1,
                numMatches: 1,
              },
            ],
            countMatches: true,
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
  ],
};
