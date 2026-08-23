# Fetches the Khronos glTF sample assets into Assets/.
#
# Not a submodule: the set is several gigabytes and only a handful of models are ever used, so pulling
# it is a deliberate action rather than something every clone pays for. Assets/ is git ignored.
#
# Uses a shallow clone, which is what keeps the download to the working tree rather than its whole
# history.

[CmdletBinding()]
param
(
	# Re-clones from scratch. Useful when a previous attempt was interrupted and left a partial tree.
	[switch] $Force
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$AssetsRoot = Join-Path $RepoRoot 'Assets'
$Destination = Join-Path $AssetsRoot 'glTF-Sample-Assets'
$RepositoryUrl = 'https://github.com/KhronosGroup/glTF-Sample-Assets.git'

if ($Force -and (Test-Path $Destination))
{
	Write-Host "Removing $Destination..." -ForegroundColor Yellow
	Remove-Item $Destination -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $AssetsRoot | Out-Null

if (Test-Path (Join-Path $Destination '.git'))
{
	# Already present: update rather than re-download. A shallow clone needs --depth on the pull as
	# well, or git fetches the entire history it was created without.
	Write-Host 'Updating the existing sample assets...' -ForegroundColor Cyan
	Push-Location $Destination
	try
	{
		git pull --depth 1 --ff-only
		if ($LASTEXITCODE -ne 0)
		{
			throw "git pull failed with exit code $LASTEXITCODE. Re-run with -Force to start over."
		}
	}
	finally
	{
		Pop-Location
	}
}
else
{
	if (Test-Path $Destination)
	{
		throw "$Destination exists but is not a git clone. Re-run with -Force to replace it."
	}

	Write-Host 'Cloning the glTF sample assets (this downloads several gigabytes)...' -ForegroundColor Cyan
	git clone --depth 1 $RepositoryUrl $Destination
	if ($LASTEXITCODE -ne 0)
	{
		throw "git clone failed with exit code $LASTEXITCODE."
	}
}

$ModelsRoot = Join-Path $Destination 'Models'
if (-not (Test-Path $ModelsRoot))
{
	throw "The clone completed but $ModelsRoot is missing; the upstream layout may have changed."
}

$ModelCount = (Get-ChildItem $ModelsRoot -Directory).Count
Write-Host "Sample assets are ready: $ModelCount models under Assets/glTF-Sample-Assets/Models." -ForegroundColor Green
Write-Host 'Reference one from ProjectSettings.json, for example:' -ForegroundColor DarkGray
Write-Host '  "scene": { "gltf": "glTF-Sample-Assets/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb" }' -ForegroundColor DarkGray
