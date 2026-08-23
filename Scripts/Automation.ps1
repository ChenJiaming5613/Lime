# Runs the Python automation suite or a project script.
#
# A thin wrapper so the automation entry points look like the other scripts in this directory. It only
# resolves Python, puts the package on the path and forwards arguments; the logic lives in
# Programs/lime_automation.
#
# Switches are declared rather than passed through as a raw remainder, because PowerShell parses
# tokens starting with "-" before the script sees them: a bare "--attach" would arrive mangled and
# argparse would never receive it.
#
#   ./Automation.ps1 test                       # pytest suite
#   ./Automation.ps1 shell -Attach              # interactive shell against a running engine
#   ./Automation.ps1 shell -Port 5613           # or a specific one
#   ./Automation.ps1 run triangle -Attach

[CmdletBinding()]
param(
	# test: the pytest suite. run/list/info/shell: the lime_automation CLI.
	[Parameter(Position = 0)]
	[ValidateSet('test', 'run', 'list', 'info', 'shell')]
	[string]$Action = 'test',

	[string[]]$Backend,
	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Config = 'Debug',
	[string]$Project = 'HelloTriangle',

	# Use a running engine rather than launching one. Ignored by 'test' and 'list'.
	[switch]$Attach,
	# Attach to this port directly, skipping discovery.
	[int]$Port,

	# Everything after the known parameters is handed to pytest or the CLI unchanged.
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]]$Remaining
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$ProgramsDir = Join-Path $RepoRoot 'Programs'
$PythonTestDir = Join-Path $RepoRoot 'Tests/Python'

$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python)
{
	$Python = Get-Command py -ErrorAction SilentlyContinue
	if (-not $Python)
	{
		throw 'No Python interpreter found. Install Python 3.10 or newer.'
	}
}

# Lets the suite and the CLI import the package without installing it.
$OriginalPythonPath = $env:PYTHONPATH
if ($OriginalPythonPath)
{
	$env:PYTHONPATH = "$ProgramsDir;$OriginalPythonPath"
}
else
{
	$env:PYTHONPATH = $ProgramsDir
}

try
{
	if ($Action -eq 'test')
	{
		& $Python.Source -c 'import pytest' 2>$null
		if ($LASTEXITCODE -ne 0)
		{
			throw "pytest is not installed. Run: $($Python.Source) -m pip install -r Programs/lime_automation/requirements.txt"
		}

		Push-Location $PythonTestDir
		try
		{
			$Arguments = @('-m', 'pytest', '--config', $Config)
			foreach ($Item in $Backend)
			{
				$Arguments += @('--backend', $Item)
			}
			$Arguments += @('--project', $Project)
			if ($Remaining) { $Arguments += $Remaining }

			& $Python.Source @Arguments
			exit $LASTEXITCODE
		}
		finally
		{
			Pop-Location
		}
	}

	$Arguments = @('-m', 'lime_automation', $Action, '--config', $Config, '--project', $Project)
	# The CLI takes a single backend, unlike pytest which accepts a matrix.
	if ($Backend) { $Arguments += @('--backend', $Backend[0]) }
	# 'list' only reads the project directory, so neither switch applies to it.
	if ($Action -ne 'list')
	{
		if ($Attach) { $Arguments += '--attach' }
		if ($PSBoundParameters.ContainsKey('Port')) { $Arguments += @('--port', $Port) }
	}
	if ($Remaining) { $Arguments += $Remaining }

	& $Python.Source @Arguments
	exit $LASTEXITCODE
}
finally
{
	$env:PYTHONPATH = $OriginalPythonPath
}
