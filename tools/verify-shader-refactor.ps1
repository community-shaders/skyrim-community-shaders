<#
.SYNOPSIS
    Prove an HLSL refactor is behavior-preserving by comparing compiled bytecode.

.DESCRIPTION
    Compiles a shader from a base git revision and from the current working tree
    across a set of preprocessor permutations, then compares the resulting DXBC.
    The base ref's entire include tree (-IncludeDir) is materialized via git archive,
    so the base compiles against base-ref headers and the working tree against working
    headers -- a refactor that also edits a shared .hlsli is therefore compared correctly.

    Tier 1 (this script): identical SHA-256 of the compiled .cso == provably identical
    GPU program. fxc emits no timestamps without /Zi, so same source -> same bytes.

    Tier 2 (this script, on mismatch): dumps /Fc assembly for both revisions and lists
    the differing lines (base/work markers), so a legitimate-but-non-identical refactor
    (e.g. register reorder) can be eyeballed.

    A refactor that is Tier-1 IDENTICAL on the swept permutations needs no further proof.
    Note: the default sweep (HDR_OUTPUT x stage) is strong evidence, not the full build
    matrix from shader-validation.yaml. Pass -Permutations for exotic define combos.

.PARAMETER Shader
    Path to the .hlsl file (repo-relative or absolute).

.PARAMETER BaseRef
    Git ref to treat as "before". Default: merge-base of HEAD and origin/dev.

.PARAMETER IncludeDir
    Shader include root passed to fxc /I. Default: package/Shaders.

.PARAMETER Permutations
    Optional explicit permutation list; each entry is a space-separated define set,
    e.g. -Permutations "PSHADER","PSHADER HDR_OUTPUT". Overrides the auto sweep.

.PARAMETER Entry
    Shader entry point. Default: main.

.PARAMETER Profile
    fxc target profile. Default: auto (cs_5_0 for *CS.hlsl, else ps_5_0).

.EXAMPLE
    pwsh tools/verify-shader-refactor.ps1 package/Shaders/ISTemporalAA.hlsl

.EXAMPLE
    pwsh tools/verify-shader-refactor.ps1 package/Shaders/Foo.hlsl -BaseRef HEAD~1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Shader,
    [string]$BaseRef,
    [string]$IncludeDir = "package/Shaders",
    [string[]]$Permutations,
    [string]$Entry = "main",
    [string]$Profile,
    [string]$Fxc
)

# Continue (not Stop): native git calls write warnings to stderr that would otherwise
# abort the run; control flow keys off explicit $LASTEXITCODE checks and `throw`.
$ErrorActionPreference = "Continue"

function Resolve-Fxc {
    if ($Fxc -and (Test-Path $Fxc)) { return $Fxc }
    $cmd = Get-Command fxc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")
    $found = foreach ($r in $roots) {
        if (Test-Path $r) {
            Get-ChildItem -Path $r -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "x64" }
        }
    }
    $pick = $found | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $pick) { throw "fxc.exe not found. Install the Windows 10/11 SDK or pass -Fxc." }
    return $pick.FullName
}

# Resolve repo root so the script works from any cwd.
$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw "Not inside a git repository." }
Push-Location $repoRoot
$work = $null
try {
    $fxcPath = Resolve-Fxc

    # Normalize the shader path to repo-relative (forward slashes) for git.
    # (Path.GetRelativePath is unavailable in Windows PowerShell 5.1 / .NET Framework.)
    $shaderFull = (Resolve-Path $Shader).Path
    $rootFull = (Resolve-Path $repoRoot).Path
    if (-not $shaderFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Shader '$Shader' resolves outside the repo root '$rootFull'."
    }
    $relPath = $shaderFull.Substring($rootFull.Length).TrimStart('\', '/').Replace('\', '/')

    if (-not $BaseRef) {
        $BaseRef = (git merge-base HEAD origin/dev 2>$null)
        if (-not $BaseRef) { $BaseRef = "HEAD" }
    }

    if (-not $Profile) {
        $Profile = if ($relPath -match 'CS\.hlsl$') { "cs_5_0" } else { "ps_5_0" }
    }
    $stageDefine = switch -Wildcard ($Profile) {
        "cs_*" { "CSHADER" }
        "vs_*" { "VSHADER" }
        default { "PSHADER" }
    }

    if (-not $Permutations -or $Permutations.Count -eq 0) {
        $Permutations = @(
            "$stageDefine",
            "$stageDefine HDR_OUTPUT"
        )
    }

    # Materialize the base revision's FULL include tree (not just the target shader) so a
    # refactor that also touches a shared .hlsli is compared correctly: base compiles against
    # base-ref headers, work compiles against working-tree headers. git archive -> tar (via a
    # file, never a PS pipeline, which would corrupt the binary tar).
    $work = Join-Path ([IO.Path]::GetTempPath()) ("shaderverify_" + [Guid]::NewGuid().ToString("N"))
    $baseRoot = Join-Path $work "base"
    New-Item -ItemType Directory -Force $baseRoot | Out-Null
    $tar = Join-Path $work "base.tar"
    git archive --format=tar -o $tar $BaseRef -- $IncludeDir $relPath 2>$null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tar)) {
        throw "git archive failed for '$BaseRef' (paths: $IncludeDir, $relPath)."
    }
    tar -xf $tar -C $baseRoot
    if ($LASTEXITCODE -ne 0) { throw "Failed to extract base archive." }
    $baseFile = Join-Path $baseRoot $relPath
    $baseInclude = Join-Path $baseRoot $IncludeDir
    if (-not (Test-Path $baseFile)) { throw "'$relPath' not found at '$BaseRef'." }

    function Compile([string]$src, [string]$incDir, [string]$defs, [string]$outFile, [switch]$Asm) {
        # Preserve explicit-valued defines (e.g. SHADOWFILTER=0); only bare names get =1.
        $defArgs = @()
        foreach ($d in ($defs -split '\s+' | Where-Object { $_ })) {
            $defArgs += "/D"
            $defArgs += $(if ($d -like '*=*') { $d } else { "$d=1" })
        }
        $fmt = if ($Asm) { "/Fc" } else { "/Fo" }
        $out = & $fxcPath /nologo /T $Profile /E $Entry @defArgs /I $incDir $src $fmt $outFile 2>&1
        return @{ Code = $LASTEXITCODE; Out = $out }
    }

    Write-Host "Shader   : $relPath"
    Write-Host "Base ref : $BaseRef  (full include tree materialized)"
    Write-Host "Profile  : $Profile  (entry $Entry)"
    Write-Host "Include  : $IncludeDir"
    Write-Host ("-" * 60)

    $allIdentical = $true
    $anyError = $false

    foreach ($perm in $Permutations) {
        $tag = $perm
        $baseCso = Join-Path $work "base.cso"
        $workCso = Join-Path $work "work.cso"
        $rb = Compile $baseFile $baseInclude $perm $baseCso
        $rw = Compile $shaderFull $IncludeDir $perm $workCso

        if ($rb.Code -ne 0 -or $rw.Code -ne 0) {
            $anyError = $true
            $which = if ($rb.Code -ne 0) { "BASE" } else { "WORK" }
            Write-Host "[$tag] COMPILE-ERROR ($which)" -ForegroundColor Red
            ($(if ($rb.Code -ne 0) { $rb.Out } else { $rw.Out }) | Where-Object { $_ -match 'error|warning' } | Select-Object -First 6) |
                ForEach-Object { Write-Host "    $_" }
            continue
        }

        $hb = (Get-FileHash $baseCso -Algorithm SHA256).Hash
        $hw = (Get-FileHash $workCso -Algorithm SHA256).Hash
        if ($hb -eq $hw) {
            Write-Host "[$tag] IDENTICAL" -ForegroundColor Green
        } else {
            $allIdentical = $false
            Write-Host "[$tag] DIFFERS  base=$($hb.Substring(0,12)) work=$($hw.Substring(0,12))" -ForegroundColor Yellow
            # Tier 2: assembly diff for inspection (Compare-Object avoids git's CRLF/exit noise).
            $baseAsm = Join-Path $work "base.asm"; $workAsm = Join-Path $work "work.asm"
            Compile $baseFile $baseInclude $perm $baseAsm -Asm | Out-Null
            Compile $shaderFull $IncludeDir $perm $workAsm -Asm | Out-Null
            $d = Compare-Object (Get-Content $baseAsm) (Get-Content $workAsm)
            if ($d) {
                $d | Select-Object -First 40 | ForEach-Object {
                    $mark = if ($_.SideIndicator -eq '=>') { 'work' } else { 'base' }
                    Write-Host ("    [{0}] {1}" -f $mark, $_.InputObject)
                }
                if (@($d).Count -gt 40) { Write-Host ("    ... (+{0} more asm lines)" -f (@($d).Count - 40)) }
            }
        }
    }

    Write-Host ("-" * 60)
    if ($anyError) { Write-Host "RESULT: compile error" -ForegroundColor Red; $exit = 1 }
    elseif ($allIdentical) { Write-Host "RESULT: behavior-preserving (all permutations identical)" -ForegroundColor Green; $exit = 0 }
    else { Write-Host "RESULT: bytecode differs - inspect asm diff above" -ForegroundColor Yellow; $exit = 2 }

    exit $exit
}
finally {
    # Runs on normal exit and on throw, so the temp dir never leaks.
    if ($work -and (Test-Path $work)) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
    Pop-Location
}
