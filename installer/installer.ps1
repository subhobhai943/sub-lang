#Requires -Version 5.1
<#
.SYNOPSIS
    SUB-LANG Universal Installer for Windows

.DESCRIPTION
    Usage:
      irm https://raw.githubusercontent.com/subhobhai943/sub-lang/main/installer/install.ps1 | iex

    With options:
      & ([scriptblock]::Create((irm https://raw.githubusercontent.com/subhobhai943/sub-lang/main/installer/install.ps1))) -Yes -Prefix "C:sub-lang"

.PARAMETER Yes
    Skip interactive prompts (license acceptance).
.PARAMETER Prefix
    Install location (default: %LOCALAPPDATA%sub-lang).
.PARAMETER NoPath
    Do not modify the user PATH.
.PARAMETER Version
    Show installer version and exit.
#>

[CmdletBinding()]
param(
    [switch]$Yes,
    [string]$Prefix = "",
    [switch]$NoPath,
    [switch]$Version,
    [switch]$Help
)

$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"   # much faster Invoke-WebRequest on PS 5.1

$InstallerVersion = "1.0.0"
$GithubRepo       = "subhobhai943/sub-lang"

try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}

# -----------------------------------------------------------------------------
# Utility Functions
# -----------------------------------------------------------------------------

function Write-Info($msg) { Write-Host "info" -ForegroundColor Green -NoNewline; Write-Host ": $msg" }
function Write-Warn($msg) { Write-Host "warn" -ForegroundColor Yellow -NoNewline; Write-Host ": $msg" }
function Fail($msg)       { throw "error: $msg" }

function Show-Banner {
    Write-Host @'
  ____  _   _ ____    _                       
 / ___|| | | | __ )  | |    __ _ _ __   __ _ 
 ___ | | | |  _   | |   / _` | '_  / _` |
  ___) | |_| | |_) | | |__| (_| | | | | (_| |
 |____/ ___/|____/  |_______,_|_| |_|__, |
                                        |___/ 
'@ -ForegroundColor Cyan
}

function Show-Help {
    Write-Host "Usage: install.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Help            Show this help message"
    Write-Host "  -Version         Show installer version"
    Write-Host "  -Prefix PATH     Install to custom path (default: %LOCALAPPDATA%sub-lang)"
    Write-Host "  -Yes             Skip interactive prompts (license acceptance)"
    Write-Host "  -NoPath          Do not add the bin directory to your user PATH"
}

function Get-WebFile($Url, $OutFile) {
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing -Headers @{ "User-Agent" = "sub-lang-installer" }
}

function Get-LatestTag {
    $tag = $null
    try {
        $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$GithubRepo/releases/latest" `
                                 -Headers @{ "User-Agent" = "sub-lang-installer" }
        $tag = $rel.tag_name
    } catch {}

    if (-not $tag) {
        # Fallback: follow /releases/latest redirect (no API rate limit)
        try {
            $resp = Invoke-WebRequest -Uri "https://github.com/$GithubRepo/releases/latest" `
                                      -UseBasicParsing -MaximumRedirection 5 `
                                      -Headers @{ "User-Agent" = "sub-lang-installer" }
            $final = $resp.BaseResponse.ResponseUri
            if (-not $final) { $final = $resp.BaseResponse.RequestMessage.RequestUri }
            if ($final -and $final.AbsoluteUri -match "/tag/([^/]+)$") { $tag = $Matches[1] }
        } catch {}
    }
    return $tag
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

function Install-SubLang {
    if ($Help)    { Show-Help; return }
    if ($Version) { Write-Host "sub-lang installer version $InstallerVersion"; return }

    # Platform detection
    if ($env:OS -ne "Windows_NT") { Fail "This installer is for Windows. Use install.sh on Linux/macOS." }

    $rawArch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    switch -Regex ($rawArch) {
        "^(AMD64|x86_64)$" { $Arch = "x86_64"; break }
        "^ARM64$"          { $Arch = "arm64";  break }
        default            { Fail "Unsupported architecture: $rawArch" }
    }
    Write-Info "Detected platform: windows-$Arch"

    if (-not $Prefix) { $Prefix = Join-Path $env:LOCALAPPDATA "sub-lang" }
    $Prefix = [Environment]::ExpandEnvironmentVariables($Prefix)
    Write-Info "Install prefix: $Prefix"

    Show-Banner

    # License acceptance
    if (-not $Yes) {
        Write-Host "SUB-LANG is distributed under the MIT License."
        Write-Host ""
        Write-Host "Copyright (c) $(Get-Date -Format yyyy) SUB Language Project"
        Write-Host ""
        Write-Host "Permission is hereby granted, free of charge, to any person obtaining a copy"
        Write-Host 'of this software and associated documentation files (the "Software"), to deal'
        Write-Host "in the Software without restriction, including without limitation the rights"
        Write-Host "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell"
        Write-Host "copies of the Software, and to permit persons to whom the Software is"
        Write-Host "furnished to do so, subject to the following conditions:"
        Write-Host ""
        Write-Host "The above copyright notice and this permission notice shall be included in all"
        Write-Host "copies or substantial portions of the Software."
        Write-Host ""
        $answer = Read-Host "Do you accept the license terms? [y/N]"
        if ($answer -notmatch "^(y|yes)$") { Fail "Installation aborted by user." }
    }

    # Release info
    Write-Info "Fetching latest release information..."
    $tag = Get-LatestTag
    if (-not $tag) { Fail "Could not determine the latest release. Check https://github.com/$GithubRepo/releases" }
    Write-Info "Target version: $tag"

    $baseUrl      = "https://github.com/$GithubRepo/releases/download/$tag"
    $checksumUrl  = "$baseUrl/checksums-sha256.txt"
    $candidates   = @("sub-windows-$Arch.zip", "sub-windows-$Arch.tar.gz")

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("sub-lang-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tmp | Out-Null

    try {
        # Download
        $fileName = $null
        $archive  = $null
        foreach ($name in $candidates) {
            $url  = "$baseUrl/$name"
            $dest = Join-Path $tmp $name
            Write-Info "Downloading $url ..."
            try {
                Get-WebFile $url $dest
                $fileName = $name; $archive = $dest
                break
            } catch {
                Write-Warn "Not found: $name"
            }
        }
        if (-not $archive) { Fail "Failed to download a Windows build from release $tag. Tried: $($candidates -join ', ')" }

        # Checksum
        $sumFile = Join-Path $tmp "sha256sums.txt"
        try {
            Get-WebFile $checksumUrl $sumFile
            Write-Info "Verifying checksum..."
            $line = Get-Content $sumFile | Where-Object { $_ -like "*$fileName*" } | Select-Object -First 1
            if ($line) {
                $expected = ($line -split "s+")[0].ToLower()
                $actual   = (Get-FileHash -Path $archive -Algorithm SHA256).Hash.ToLower()
                if ($expected -ne $actual) { Fail "Checksum verification failed!" }
                Write-Info "Checksum OK."
            } else {
                Write-Warn "No checksum entry for $fileName. Skipping verification."
            }
        } catch [System.Net.WebException] {
            Write-Warn "No checksum file found at release. Skipping verification."
        } catch {
            if ($_.Exception.Message -like "error:*") { throw }
            Write-Warn "No checksum file found at release. Skipping verification."
        }

        # Extract
        Write-Info "Extracting and installing to $Prefix..."
        $extract = Join-Path $tmp "extracted"
        New-Item -ItemType Directory -Path $extract | Out-Null

        if ($fileName.EndsWith(".zip")) {
            Expand-Archive -Path $archive -DestinationPath $extract -Force
        } else {
            if (-not (Get-Command tar -ErrorAction SilentlyContinue)) { Fail "tar not found; cannot extract $fileName." }
            & tar -xzf $archive -C $extract
            if ($LASTEXITCODE -ne 0) { Fail "Failed to extract archive" }
        }

        # Binaries may be at the root or in a single top-level directory
        $inner = $extract
        $hasBin = Get-ChildItem -Path $inner -File -ErrorAction SilentlyContinue |
                  Where-Object { $_.Name -in @("sub.exe","subc.exe","subi.exe") }
        if (-not $hasBin) {
            $sub = Get-ChildItem -Path $extract -Directory | Select-Object -First 1
            if ($sub) { $inner = $sub.FullName }
        }

        $binDir    = Join-Path $Prefix "bin"
        $stdlibDir = Join-Path $Prefix "libsubstdlib"
        New-Item -ItemType Directory -Force -Path $binDir, $stdlibDir | Out-Null

        $installed = 0
        foreach ($tool in @("sub", "subc", "subi")) {
            $src = Join-Path $inner "$tool.exe"
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination (Join-Path $binDir "$tool.exe") -Force
                $installed++
            }
        }
        if ($installed -eq 0) { Fail "No binaries (sub.exe, subc.exe, subi.exe) found in the archive." }

        $srcStd = Join-Path $inner "stdlib"
        if (Test-Path $srcStd) {
            Copy-Item -Path (Join-Path $srcStd "*") -Destination $stdlibDir -Recurse -Force
        }

        # Smoke test
        Write-Info "Running smoke test..."
        $subi = Join-Path $binDir "subi.exe"
        if (Test-Path $subi) {
            try {
                & $subi --version *> $null
                if ($LASTEXITCODE -eq 0) { Write-Info "Smoke test passed successfully." }
                else { Write-Warn "Smoke test failed. The binary was installed but did not execute properly." }
            } catch {
                Write-Warn "Smoke test failed. The binary was installed but did not execute properly."
            }
        } else {
            Write-Warn "Could not locate 'subi.exe' in $binDir for smoke test."
        }

        # PATH
        Write-Info "Installation complete!"
        Write-Host ""
        Write-Host "SUB-LANG has been successfully installed!" -ForegroundColor Green
        Write-Host ""

        $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
        $entries  = @()
        if ($userPath) { $entries = $userPath -split ";" | Where-Object { $_ } }
        $onPath   = $entries | Where-Object { $_.TrimEnd("") -ieq $binDir.TrimEnd("") }

        if ($onPath) {
            Write-Info "$binDir is already in your PATH."
        } elseif ($NoPath) {
            Write-Host "Add $binDir to your PATH, e.g.:"
            Write-Host "  [Environment]::SetEnvironmentVariable('Path', `"$binDir;`" + [Environment]::GetEnvironmentVariable('Path','User'), 'User')"
        } else {
            $newPath = if ($userPath) { "$userPath;$binDir" } else { $binDir }
            [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
            $env:Path = "$env:Path;$binDir"
            Write-Info "Added $binDir to your user PATH. Restart open terminals to pick it up."
        }

        Write-Host ""
        Write-Host "Happy coding with SUB-LANG!"
    }
    finally {
        Remove-Item -Recurse -Force -Path $tmp -ErrorAction SilentlyContinue
    }
}

try {
    Install-SubLang
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if ($MyInvocation.InvocationName -ne "&" -and $Host.Name -eq "ConsoleHost") { }
    return
}
