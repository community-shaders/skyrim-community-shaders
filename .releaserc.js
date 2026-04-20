const releaseType = process.env.RELEASE_TYPE || 'rc';

module.exports = {
  branches: [
    {
      name: 'dev',
      ...(releaseType === 'rc' ? { prerelease: 'rc' } : {}),
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
            to: 'VERSION ${nextRelease.version}',
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
