# Troubleshooting Test Scripts

## Common Issues

### PowerShell Parser Errors

**Error: "Unexpected token"** or similar parser errors

**Cause:** Script encoding issues or execution policy

**Solutions:**

1. **Check execution policy:**

```powershell
Get-ExecutionPolicy
# Should be RemoteSigned or Unrestricted

# If Restricted, run:
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

2. **Run with explicit bypass:**

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_tests.ps1
```

3. **Check for BOM or encoding issues:**

```powershell
# Re-save with UTF-8 encoding
Get-Content .\tests\run_tests.ps1 | Out-File .\tests\run_tests.ps1 -Encoding UTF8
```

### Test Executable Not Found

**Error: "❌ Could not find test executable"**

**Cause:** Tests haven't been built yet

**Solution:**

```bash
# Build tests first
cmake --preset=ALL -DBUILD_TESTS=ON
cmake --build build/ALL --target CommunityShaders_Tests --config Release

# Then run
.\tests\run_tests.ps1
```

### CMake Build Errors

**Error: Build failures during test compilation**

**Solutions:**

1. **Check CMake version:**

```bash
cmake --version  # Should be 3.21+
```

2. **Reconfigure from scratch:**

```bash
Remove-Item -Recurse build/ALL
cmake --preset=ALL -DBUILD_TESTS=ON
cmake --build build/ALL --target CommunityShaders_Tests --config Release
```

3. **Check vcpkg:**

```bash
$env:VCPKG_ROOT  # Should point to your vcpkg installation
```

### Coverage Script Errors

**Error: "clang++ not found"** or "llvm-profdata not found"

**Cause:** Clang/LLVM not installed

**Solution:**

```powershell
# Install LLVM
winget install LLVM.LLVM

# Verify installation
clang++ --version
llvm-profdata --version
llvm-cov --version
```

### Path Issues

**Error: Scripts fail when run from different directories**

**Solution:** The scripts now use absolute paths. If you still have issues:

```powershell
# Always run from project root
cd E:\Documents\source\repos\skyrim-community-shaders
.\tests\run_tests.ps1
```

## Verification Steps

Test each component individually:

```powershell
# 1. Check PowerShell version (should be 5.1+)
$PSVersionTable.PSVersion

# 2. Check CMake
cmake --version

# 3. Check test build
cmake --build build/ALL --target CommunityShaders_Tests --config Release

# 4. Check test executable
Test-Path build\ALL\tests\Release\CommunityShaders_Tests.exe

# 5. Run tests directly
.\build\ALL\tests\Release\CommunityShaders_Tests.exe

# 6. If all above work, try script
.\tests\run_tests.ps1
```

## Still Having Issues?

1. **Share error details:**

    - Exact command you ran
    - Full error message
    - PowerShell version
    - Windows version

2. **Try manual test run:**

```bash
# Build manually
cmake --build build/ALL --target CommunityShaders_Tests --config Release

# Run manually
cd build\ALL\tests\Release
.\CommunityShaders_Tests.exe
```

3. **Check logs:**

```bash
# CMake logs
type build\ALL\CMakeFiles\CMakeOutput.log

# Build logs
# Check console output during build
```

## Quick Fixes

**Just want to run tests?**

```powershell
# Simplest approach - run directly
.\build\ALL\tests\Release\CommunityShaders_Tests.exe
```

**Script won't run at all?**

```powershell
# Copy and paste script content directly into PowerShell
```

**Coverage not needed?**

```bash
# Just use pre-commit hook
git commit  # Tests run automatically
```
