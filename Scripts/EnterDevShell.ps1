# Loads the Visual Studio 2022 x64 developer environment into the current session.
# Idempotent: does nothing when the MSVC toolset is already reachable.

if (Get-Command cl.exe -ErrorAction SilentlyContinue)
{
	return
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere))
{
	throw 'vswhere.exe not found. Install Visual Studio 2022 with the C++ workload.'
}

$VsPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VsPath)
{
	throw 'No Visual Studio installation with the MSVC x64 toolset was found.'
}

$VcVars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $VcVars))
{
	throw "vcvars64.bat not found under $VsPath."
}

# vcvars silently skips the compiler paths when Microsoft.VCToolsVersion.default.txt points at a
# toolset that is not actually installed. Detect that case and pin an installed version instead.
$ToolsRoot = Join-Path $VsPath 'VC\Tools\MSVC'
$Installed = Get-ChildItem $ToolsRoot -Directory -ErrorAction SilentlyContinue |
	Where-Object { Test-Path (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') }
if (-not $Installed)
{
	throw "No MSVC x64 toolset found under $ToolsRoot. Repair the Visual Studio C++ workload."
}

$VcVarsArgs = ''
$DefaultVersionFile = Join-Path $VsPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
$DefaultVersion = if (Test-Path $DefaultVersionFile) { (Get-Content $DefaultVersionFile -Raw).Trim() } else { '' }
if (-not $DefaultVersion -or -not ($Installed.Name -contains $DefaultVersion))
{
	$Pinned = ($Installed | Sort-Object { [version]$_.Name } | Select-Object -Last 1).Name
	$VcVarsArgs = " -vcvars_ver=$Pinned"
	Write-Host "Default MSVC toolset '$DefaultVersion' is not installed; pinning $Pinned." -ForegroundColor Yellow
}

# vcvars only exports into a cmd session, so mirror the resulting variables back here.
$Marker = '___LIME_ENV___'
$Command = 'call "' + $VcVars + '"' + $VcVarsArgs + ' && echo ' + $Marker + ' && set'
$Output = & cmd.exe /d /c $Command

$MarkerIndex = -1
for ($Index = 0; $Index -lt $Output.Count; $Index++)
{
	if ($Output[$Index].Trim() -eq $Marker)
	{
		$MarkerIndex = $Index
		break
	}
}

if ($MarkerIndex -lt 0)
{
	throw "vcvars64.bat did not run successfully:`n$($Output -join [Environment]::NewLine)"
}

foreach ($Line in $Output[($MarkerIndex + 1)..($Output.Count - 1)])
{
	$Pair = $Line -split '=', 2
	if ($Pair.Count -eq 2 -and $Pair[0])
	{
		Set-Item -Path "env:$($Pair[0])" -Value $Pair[1] -ErrorAction SilentlyContinue
	}
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
{
	throw 'MSVC environment was applied but cl.exe is still not on PATH.'
}

# Ninja and CMake ship with Visual Studio but are not always on PATH.
$Bundled = @(
	(Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'),
	(Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin')
)
foreach ($Dir in $Bundled)
{
	if ((Test-Path $Dir) -and ($env:PATH -notlike "*$Dir*"))
	{
		$env:PATH = "$env:PATH;$Dir"
	}
}

if (-not $env:VULKAN_SDK)
{
	$Sdk = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
	if ($Sdk)
	{
		$env:VULKAN_SDK = $Sdk.FullName
	}
}
