# Points the Visual Studio default toolset files at an installed toolset.
#
# Why this is needed: Microsoft.VCToolsVersion.default.txt names the toolset that vcvars, MSBuild and
# CMake use when none is requested explicitly. On this machine it names 14.38.33130, which is not
# installed, and the failure modes are all silent or misleading:
#
#   * vcvars64.bat builds a path to a directory that does not exist and quietly leaves cl.exe off PATH,
#     so any IDE that prepares the environment itself reports "compiler not found".
#   * CMake's Visual Studio generator validates the toolset during instance discovery, and when the
#     directory is missing it rejects the whole installation with "could not find any instance of
#     Visual Studio" -- which is why CLion and the vs2022 preset fail even though VS is installed.
#
# Writing the installed version into these files fixes every path at once, and makes the -vcvars_ver
# fallback in EnterDevShell.ps1 a no-op.
#
# Only the ToolsVersion files are touched. VCRedistVersion is left alone because the redist directory
# it names does exist.
#
# Run with -Revert to restore the .bak files.
#
# Note: a Visual Studio update may rewrite these files. Installing the matching toolset through the
# VS Installer is the permanent fix; this script is the local workaround.

[CmdletBinding()]
param(
	# Toolset to use. Defaults to the newest installed one.
	[string]$Version,
	[switch]$Revert,
	# Reports what would change without writing anything.
	[switch]$WhatIfOnly
)

$ErrorActionPreference = 'Stop'

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere))
{
	throw 'vswhere.exe not found. Install Visual Studio 2022 with the C++ workload.'
}

$VsPath = & $VsWhere -latest -products * -property installationPath
if (-not $VsPath)
{
	throw 'No Visual Studio installation found.'
}

$BuildDir = Join-Path $VsPath 'VC\Auxiliary\Build'
$ToolsRoot = Join-Path $VsPath 'VC\Tools\MSVC'

# Both the .txt and the .props matter: vcvars reads the former, MSBuild and the CMake Visual Studio
# generator read the latter.
$Targets = @(
	'Microsoft.VCToolsVersion.default.txt',
	'Microsoft.VCToolsVersion.v143.default.txt',
	'Microsoft.VCToolsVersion.default.props',
	'Microsoft.VCToolsVersion.v143.default.props'
) | ForEach-Object { Join-Path $BuildDir $_ } | Where-Object { Test-Path $_ }

if ($Revert)
{
	$Restored = 0
	foreach ($File in $Targets)
	{
		$Backup = "$File.bak"
		if (Test-Path $Backup)
		{
			Copy-Item $Backup $File -Force
			Remove-Item $Backup
			Write-Host "Restored $(Split-Path $File -Leaf)" -ForegroundColor Green
			$Restored++
		}
	}
	if ($Restored -eq 0) { Write-Host 'No backups found; nothing to restore.' -ForegroundColor Yellow }
	exit 0
}

# A toolset only counts as installed when the x64 host compiler is actually present.
$Installed = Get-ChildItem $ToolsRoot -Directory -ErrorAction SilentlyContinue |
	Where-Object { Test-Path (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') }

if (-not $Installed)
{
	throw "No MSVC x64 toolset found under $ToolsRoot. Repair the Visual Studio C++ workload."
}

if (-not $Version)
{
	$Version = ($Installed | Sort-Object { [version]$_.Name } | Select-Object -Last 1).Name
}
elseif ($Installed.Name -notcontains $Version)
{
	throw "Toolset $Version is not installed. Available: $($Installed.Name -join ', ')"
}

Write-Host "Visual Studio : $VsPath"
Write-Host "Installed     : $($Installed.Name -join ', ')"
Write-Host "Target        : $Version`n"

$Changed = 0
foreach ($File in $Targets)
{
	$Name = Split-Path $File -Leaf
	$Content = Get-Content $File -Raw

	# The .txt holds the bare version; the .props wraps it in a VCToolsVersion element.
	if ($Name.EndsWith('.txt'))
	{
		$Current = $Content.Trim()
		$Updated = $Version
	}
	else
	{
		if ($Content -notmatch '<VCToolsVersion[^>]*>([^<]+)</VCToolsVersion>')
		{
			Write-Host "  skip    $Name (no VCToolsVersion element)" -ForegroundColor DarkGray
			continue
		}
		$Current = $Matches[1].Trim()
		$Updated = $Content -replace '(<VCToolsVersion[^>]*>)[^<]+(</VCToolsVersion>)', "`${1}$Version`${2}"
	}

	if ($Current -eq $Version)
	{
		Write-Host "  ok      $Name already $Version" -ForegroundColor DarkGray
		continue
	}

	if ($WhatIfOnly)
	{
		Write-Host "  would   $Name : $Current -> $Version" -ForegroundColor Yellow
		continue
	}

	# Backed up once, so repeated runs keep the original rather than the last edit.
	$Backup = "$File.bak"
	if (-not (Test-Path $Backup))
	{
		Copy-Item $File $Backup
	}

	# No trailing newline in the .txt: vcvars compares the contents verbatim.
	if ($Name.EndsWith('.txt'))
	{
		[System.IO.File]::WriteAllText($File, $Updated)
	}
	else
	{
		[System.IO.File]::WriteAllText($File, $Updated)
	}

	Write-Host "  patched $Name : $Current -> $Version" -ForegroundColor Green
	$Changed++
}

if ($WhatIfOnly)
{
	Write-Host "`nDry run; nothing was written." -ForegroundColor Yellow
	exit 0
}

Write-Host "`n$Changed file(s) changed. Backups have a .bak suffix; run with -Revert to undo." -ForegroundColor Cyan
Write-Host 'Restart any IDE so it picks up the new default.' -ForegroundColor Cyan
