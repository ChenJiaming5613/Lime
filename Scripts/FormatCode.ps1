# Applies .clang-format to all engine, project and test sources. Third party code is skipped.

param(
	[switch]$Check
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

$ClangFormat = Get-Command clang-format -ErrorAction SilentlyContinue
if (-not $ClangFormat)
{
	$Candidate = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\*\VC\Tools\Llvm\x64\bin\clang-format.exe'
	$Resolved = Get-ChildItem $Candidate -ErrorAction SilentlyContinue | Select-Object -First 1
	if (-not $Resolved)
	{
		throw 'clang-format not found. Install LLVM or the Visual Studio clang tooling component.'
	}
	$ClangFormat = $Resolved.FullName
}
else
{
	$ClangFormat = $ClangFormat.Source
}

$Directories = @('Engine', 'Projects', 'Tests') | ForEach-Object { Join-Path $RepoRoot $_ }
$Files = Get-ChildItem -Path $Directories -Recurse -Include *.h, *.hpp, *.cpp -File -ErrorAction SilentlyContinue

if (-not $Files)
{
	Write-Host 'No sources found.' -ForegroundColor Yellow
	return
}

if ($Check)
{
	& $ClangFormat --dry-run --Werror @($Files.FullName)
	exit $LASTEXITCODE
}

& $ClangFormat -i @($Files.FullName)
Write-Host "Formatted $($Files.Count) file(s)." -ForegroundColor Green
