# Initializes all third party submodules.

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $RepoRoot
try
{
	Write-Host 'Initializing submodules...' -ForegroundColor Cyan
	git submodule update --init --recursive
	if ($LASTEXITCODE -ne 0)
	{
		throw "git submodule update failed with exit code $LASTEXITCODE."
	}
	Write-Host 'Submodules are ready.' -ForegroundColor Green
}
finally
{
	Pop-Location
}
