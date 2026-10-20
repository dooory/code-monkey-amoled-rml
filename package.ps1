# Packages the built mod as studio_theme.zip for the ModLoader launcher's "Add from .zip".
# Usage: .\package.ps1 -ModLoader C:\path\to\roblox-modloader
param(
    [Parameter(Mandatory)] [string] $ModLoader
)

$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot 'studio_theme'
$dll = Join-Path $ModLoader 'build\examples\Release\studio_theme.dll'
if (-not (Test-Path $dll)) {
    throw "Build the mod first; $dll was not found."
}

$stage = Join-Path ([IO.Path]::GetTempPath()) "studio_theme_package"
$mod = Join-Path $stage 'studio_theme'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $mod 'native') | Out-Null

Copy-Item (Join-Path $source 'mod.toml') $mod
Copy-Item (Join-Path $source 'themes') $mod -Recurse
Copy-Item (Join-Path $source 'scripts') $mod -Recurse
Copy-Item $dll (Join-Path $mod 'native')
# Starts on Code Monkey AMOLED instead of Studio's stock colors.
Set-Content (Join-Path $mod 'state.json') '{"active": "Code Monkey AMOLED"}' -NoNewline

# The zip's single root folder becomes the mod's name.
$zip = Join-Path $PSScriptRoot 'studio_theme.zip'
Compress-Archive -Path $mod -DestinationPath $zip -Force
Remove-Item $stage -Recurse -Force
Write-Host "Wrote $zip"
