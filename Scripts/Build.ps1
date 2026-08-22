# Configures and builds LimeEngine inside a Visual Studio developer environment.

param(
	[string]$Preset = 'ninja',
	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Config = 'Debug',
	[switch]$ConfigureOnly,
	[switch]$Fresh
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'EnterDevShell.ps1')

Push-Location $RepoRoot
try
{
	$ConfigureArgs = @('--preset', $Preset)
	if ($Fresh)
	{
		$ConfigureArgs += '--fresh'
	}

	cmake @ConfigureArgs
	if ($LASTEXITCODE -ne 0)
	{
		throw "Configure failed with exit code $LASTEXITCODE."
	}

	if ($ConfigureOnly)
	{
		return
	}

	cmake --build "Build/$Preset" --config $Config
	if ($LASTEXITCODE -ne 0)
	{
		throw "Build failed with exit code $LASTEXITCODE."
	}

	Write-Host "Build succeeded: Build/$Preset/Bin/$Config/<Project>" -ForegroundColor Green
}
finally
{
	Pop-Location
}
