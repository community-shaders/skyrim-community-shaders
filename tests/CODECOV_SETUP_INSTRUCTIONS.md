# Setting Up Codecov for Your Repository

Follow these steps to enable free code coverage tracking for your open source project.

## Prerequisites

-   Repository must be public OR you have a Codecov account
-   Repository must be on GitHub, GitLab, or Bitbucket
-   You must be a repository administrator

## Step 1: Sign Up for Codecov

1. Go to https://codecov.io
2. Click "Sign up with GitHub"
3. Authorize Codecov to access your repositories

**Cost:** FREE for open source projects! 🎉

## Step 2: Add Your Repository

1. In Codecov dashboard, click "Add Repository"
2. Find `skyrim-community-shaders` in the list
3. Click "Setup repo"
4. You'll see a upload token - **SAVE THIS!**

## Step 3: Add Secret to GitHub

1. Go to your GitHub repository settings
2. Navigate to: **Settings** → **Secrets and variables** → **Actions**
3. Click **"New repository secret"**
4. Name: `CODECOV_TOKEN`
5. Value: _paste the token from Codecov_
6. Click **"Add secret"**

## Step 4: Test It Out

The coverage workflow is already configured! To test it:

1. Push a commit to `main` or `dev`, OR
2. Open a pull request

The coverage job will:

-   ✅ Build tests with Clang
-   ✅ Generate coverage report
-   ✅ Upload to Codecov
-   ✅ Comment on PR with results

## Step 5: Add Badge to README (Optional)

Make your coverage visible with a badge!

1. In Codecov dashboard, go to your repo settings
2. Click "Badge" in the left sidebar
3. Copy the Markdown code
4. Add to your `README.md`:

```markdown
[![codecov](https://codecov.io/gh/YOUR_USERNAME/skyrim-community-shaders/branch/main/graph/badge.svg)](https://codecov.io/gh/YOUR_USERNAME/skyrim-community-shaders)
```

## What You Get

### PR Comments

Every pull request gets an automatic comment showing:

-   Coverage percentage
-   Coverage change vs base branch
-   Link to detailed report
-   Which files changed coverage

### Web Dashboard

Beautiful visualizations:

-   Line-by-line coverage
-   Coverage trends over time
-   Commit history
-   Sunburst charts
-   Coverage graphs

### File Browser

Click any file to see:

-   Which lines are covered (green)
-   Which lines are not covered (red)
-   Which lines are partially covered (yellow)

## Configuration

Coverage settings are in `.codecov.yml`:

-   **Target:** Auto (maintains current level)
-   **Threshold:** 1% (small drops are okay)
-   **Informational:** Won't fail CI on coverage drops
-   **Patch target:** 80% for new code

You can adjust these as needed!

## Troubleshooting

### "No coverage uploaded yet"

-   Check that GitHub secret `CODECOV_TOKEN` is set correctly
-   Verify the coverage workflow ran (check Actions tab)
-   Look for errors in the "Upload Coverage to Codecov" step

### "Upload failed"

-   Token might be incorrect - regenerate in Codecov settings
-   Check that the `.lcov` file was generated (check artifacts)
-   Try with `fail_ci_if_error: false` (already set)

### Coverage seems wrong

-   Check `.codecov.yml` ignore patterns
-   Verify test files aren't being measured
-   Look at the HTML report artifact to debug

## Optional: Branch Protection

Want to enforce coverage standards?

1. Go to: **Settings** → **Branches** → **Branch protection rules**
2. Add rule for `main` branch
3. Require status checks: ✅ `codecov/project`
4. Now PRs must maintain coverage to merge!

## Support

-   [Codecov Docs](https://docs.codecov.com/)
-   [Codecov Community](https://community.codecov.com/)
-   Questions? Open an issue in this repo!

---

**That's it!** Once the token is added, coverage will automatically track on all PRs. 🚀
