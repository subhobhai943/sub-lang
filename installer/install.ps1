#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Yes,
    [string]$Prefix = "",
    [switch]$NoPath,
    [switch]$Version,
    [switch]$Help
)

$ErrorActionPreference = "Stop"
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}

$scriptUrl = "https://raw.githubusercontent.com/subhobhai943/sub-lang/main/installer/installer.ps1"
$raw = Invoke-WebRequest -Uri $scriptUrl -UseBasicParsing -Headers @{ "User-Agent" = "sub-lang-installer" }
$installer = [scriptblock]::Create($raw.Content)
& $installer @PSBoundParameters
