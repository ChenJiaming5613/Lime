# Runs clang-tidy over engine, project and test sources using a compile database.

param(
	[string]$BuildDir = 'Build/ninja',
	[switch]$Fix
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Database = Join-Path $RepoRoot (Join-Path $BuildDir 'compile_commands.json')

if (-not (Test-Path $Database))
{
	throw "compile_commands.json not found at $Database. Configure with the 'ninja' preset first."
}

$ClangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
if (-not $ClangTidy)
{
	$Candidate = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin\clang-tidy.exe'
	$Resolved = Get-ChildItem $Candidate -ErrorAction SilentlyContinue | Select-Object -First 1
	if (-not $Resolved)
	{
		throw 'clang-tidy not found. Install LLVM or the Visual Studio clang tooling component.'
	}
	$ClangTidy = $Resolved.FullName
}
else
{
	$ClangTidy = $ClangTidy.Source
}

$Directories = @('Engine', 'Projects', 'Tests') | ForEach-Object { Join-Path $RepoRoot $_ }
$Files = Get-ChildItem -Path $Directories -Recurse -Include *.cpp -File -ErrorAction SilentlyContinue

if (-not $Files)
{
	Write-Host 'No sources found.' -ForegroundColor Yellow
	return
}

$Arguments = @('-p', (Split-Path -Parent $Database), '--quiet')
if ($Fix)
{
	$Arguments += '--fix'
}

$Failed = 0
foreach ($File in $Files)
{
	& $ClangTidy @Arguments $File.FullName
	if ($LASTEXITCODE -ne 0)
	{
		$Failed++
	}
}

if ($Failed -gt 0)
{
	Write-Host "clang-tidy reported issues in $Failed file(s)." -ForegroundColor Red
	exit 1
}

Write-Host "clang-tidy clean across $($Files.Count) file(s)." -ForegroundColor Green
