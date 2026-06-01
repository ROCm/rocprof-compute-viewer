param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir
)

$ErrorActionPreference = "Stop"

function Find-Dumpbin {
    $existing = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($existing) {
        return $existing.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $candidate = Get-ChildItem -Path (Join-Path $vsPath "VC\Tools\MSVC") -Filter dumpbin.exe -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "\\bin\\Hostx64\\x64\\dumpbin\.exe$" } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($candidate) {
                return $candidate.FullName
            }
        }
    }

    throw "dumpbin.exe not found"
}

function Test-IsQtDll([string] $Name) {
    return $Name -match "^Qt[0-9].*\.dll$"
}

function Test-IsQtDeploymentFile([System.IO.FileInfo] $File, [string] $Root) {
    if (Test-IsQtDll $File.Name) {
        return $true
    }

    $relative = [System.IO.Path]::GetRelativePath($Root, $File.FullName)
    return $relative -match "^plugins[\\/]"
}

function Test-IsSystemOrToolchainDll([string] $Name) {
    $lower = $Name.ToLowerInvariant()
    $known = @(
        "advapi32.dll",
        "bcrypt.dll",
        "cfgmgr32.dll",
        "comctl32.dll",
        "comdlg32.dll",
        "crypt32.dll",
        "d3d9.dll",
        "dwmapi.dll",
        "dxgi.dll",
        "gdi32.dll",
        "imm32.dll",
        "iphlpapi.dll",
        "kernel32.dll",
        "mpr.dll",
        "netapi32.dll",
        "ncrypt.dll",
        "normaliz.dll",
        "ntdll.dll",
        "ole32.dll",
        "oleaut32.dll",
        "powrprof.dll",
        "propsys.dll",
        "rpcrt4.dll",
        "secur32.dll",
        "setupapi.dll",
        "shell32.dll",
        "shlwapi.dll",
        "uiautomationcore.dll",
        "user32.dll",
        "userenv.dll",
        "usp10.dll",
        "uxtheme.dll",
        "version.dll",
        "winmm.dll",
        "winspool.drv",
        "ws2_32.dll",
        "wtsapi32.dll"
    )

    if ($known -contains $lower) {
        return $true
    }

    return $lower -match "^(api-ms-win-|ext-ms-win-|ucrtbase|vcruntime|msvcp|concrt|msvcr|msvcp_win).*\.dll$"
}

function Get-ImportedDlls([string] $Path, [string] $Dumpbin) {
    $output = & $Dumpbin /nologo /dependents $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for ${Path}: $output"
    }

    foreach ($line in $output) {
        if ($line -match "^\s+([A-Za-z0-9_.+\-]+\.dll)\s*$") {
            $matches[1]
        }
    }
}

$buildPath = (Resolve-Path $BuildDir).Path
$packageRootMarker = Join-Path $buildPath "_CPack_Packages"
$exeCandidates = @(Get-ChildItem -Path $buildPath -Filter rocprof-compute-viewer.exe -Recurse -ErrorAction SilentlyContinue)
$packagedExeCandidates = @($exeCandidates | Where-Object { $_.FullName.StartsWith($packageRootMarker, [System.StringComparison]::OrdinalIgnoreCase) })

if ($packagedExeCandidates.Count -gt 0) {
    $viewerExe = $packagedExeCandidates | Sort-Object FullName | Select-Object -First 1
} elseif ($exeCandidates.Count -gt 0) {
    $viewerExe = $exeCandidates | Sort-Object FullName | Select-Object -First 1
} else {
    throw "rocprof-compute-viewer.exe not found under $buildPath"
}

if ($viewerExe.Directory.Name -ieq "bin") {
    $packageRoot = $viewerExe.Directory.Parent.FullName
} else {
    $packageRoot = $viewerExe.Directory.FullName
}

$dumpbin = Find-Dumpbin
Write-Host "Using dumpbin: $dumpbin"
Write-Host "Inspecting packaged tree: $packageRoot"
Write-Host "Viewer executable: $($viewerExe.FullName)"

$packagedDlls = @{}
Get-ChildItem -Path $packageRoot -Recurse -File -Filter *.dll | ForEach-Object {
    $packagedDlls[$_.Name.ToLowerInvariant()] = $_.FullName
}

Write-Host ""
Write-Host "Packaged non-Qt DLLs:"
Get-ChildItem -Path $packageRoot -Recurse -File -Filter *.dll |
    Where-Object { -not (Test-IsQtDeploymentFile $_ $packageRoot) } |
    Sort-Object FullName |
    ForEach-Object { Write-Host "  $([System.IO.Path]::GetRelativePath($packageRoot, $_.FullName))" }

$binaries = @(Get-ChildItem -Path $packageRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".dll") -and -not (Test-IsQtDeploymentFile $_ $packageRoot) } |
    Sort-Object FullName)

$imports = New-Object System.Collections.Generic.List[object]

Write-Host ""
Write-Host "Non-Qt DLL imports by packaged binary:"
foreach ($binary in $binaries) {
    $relativeBinary = [System.IO.Path]::GetRelativePath($packageRoot, $binary.FullName)
    $deps = @(Get-ImportedDlls -Path $binary.FullName -Dumpbin $dumpbin |
        Where-Object { -not (Test-IsQtDll $_) } |
        Sort-Object -Unique)

    Write-Host "  ${relativeBinary}"
    if ($deps.Count -eq 0) {
        Write-Host "    <none>"
    } else {
        foreach ($dep in $deps) {
            Write-Host "    $dep"
            $imports.Add([pscustomobject]@{
                Importer = $relativeBinary
                Dll = $dep
            })
        }
    }
}

$missing = @($imports |
    Where-Object {
        $dllKey = $_.Dll.ToLowerInvariant()
        -not $packagedDlls.ContainsKey($dllKey) -and -not (Test-IsSystemOrToolchainDll $_.Dll)
    } |
    Sort-Object Dll, Importer -Unique)

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "::error::Missing packaged non-Qt DLL imports:"
    foreach ($item in $missing) {
        Write-Host "  $($item.Dll) imported by $($item.Importer)"
    }
    exit 1
}

Write-Host ""
Write-Host "All non-system, non-Qt DLL imports are present in the packaged tree."
